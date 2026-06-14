/**
 * @file ota_route.cpp
 * @brief Implementation of OtaRoute.
 */
#include "ota_route.hpp"

#include <stdio.h>
#include <string.h>

#include "api_context.hpp"
#include "cJSON.h"
#include "error_response_factory.hpp"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "http_response_helpers.hpp"
#include "ota_service.hpp"

static constexpr size_t kRequestBodyMax = 1024;

namespace {

bool extractOtaSecret(httpd_req_t* req, char* secretOut, size_t secretOutLen) {
    if (req == nullptr || secretOut == nullptr || secretOutLen == 0) {
        return false;
    }
    secretOut[0] = '\0';

    char headerSecret[96] = {};
    if (httpd_req_get_hdr_value_str(req, "X-Ota-Secret", headerSecret, sizeof(headerSecret)) == ESP_OK &&
        headerSecret[0] != '\0') {
        strncpy(secretOut, headerSecret, secretOutLen - 1);
        secretOut[secretOutLen - 1] = '\0';
        return true;
    }

    char authHeader[128] = {};
    if (httpd_req_get_hdr_value_str(req, "Authorization", authHeader, sizeof(authHeader)) == ESP_OK) {
        const char* prefix = "Bearer ";
        const size_t prefixLen = strlen(prefix);
        if (strncmp(authHeader, prefix, prefixLen) == 0 && authHeader[prefixLen] != '\0') {
            strncpy(secretOut, authHeader + prefixLen, secretOutLen - 1);
            secretOut[secretOutLen - 1] = '\0';
            return true;
        }
    }

    return false;
}

bool requireOtaEnabled(httpd_req_t* req, const ApiContext* context, const char* requestId) {
    if (context == nullptr || context->otaService == nullptr || !context->otaService->isEnabled()) {
        ErrorResponseFactory errors;
        char body[256] = {};
        errors.build("NOT_FOUND", "OTA not configured", false, requestId, body, sizeof(body));
        sendJsonResponse(req, 404, body);
        return false;
    }
    return true;
}

bool requireOtaSecret(httpd_req_t* req, const ApiContext* context, const char* requestId) {
    if (!requireOtaEnabled(req, context, requestId)) {
        return false;
    }

    char secret[96] = {};
    extractOtaSecret(req, secret, sizeof(secret));
    if (context->otaService == nullptr || !context->otaService->authorize(secret)) {
        ErrorResponseFactory errors;
        char body[256] = {};
        errors.build("UNAUTHORIZED", "Missing or invalid OTA secret", false, requestId, body, sizeof(body));
        sendJsonResponse(req, 401, body);
        return false;
    }
    return true;
}

bool readBody(httpd_req_t* req, char* buffer, size_t bufferLen, size_t& outLen) {
    outLen = 0;
    if (req == nullptr || buffer == nullptr || bufferLen == 0) {
        return false;
    }

    const int total = req->content_len;
    if (total < 0 || static_cast<size_t>(total) >= bufferLen) {
        return false;
    }
    if (total == 0) {
        buffer[0] = '\0';
        return true;
    }

    int received = 0;
    while (received < total) {
        const int chunk = httpd_req_recv(req, buffer + received, total - received);
        if (chunk <= 0) {
            return false;
        }
        received += chunk;
    }
    buffer[received] = '\0';
    outLen = static_cast<size_t>(received);
    return true;
}

const char* stateLabel(OtaState state) {
    switch (state) {
        case OtaState::Downloading:
            return "downloading";
        case OtaState::Rebooting:
            return "rebooting";
        case OtaState::Failed:
            return "failed";
        case OtaState::Idle:
        default:
            return "idle";
    }
}

}  // namespace

bool OtaRoute::registerRoutes(httpd_handle_t server, const ApiContext* context) const {
    if (server == nullptr || context == nullptr) {
        return false;
    }

    httpd_uri_t statusUri = {
        .uri = "/api/v1/ota/status",
        .method = HTTP_GET,
        .handler = handleGetStatus,
        .user_ctx = const_cast<ApiContext*>(context),
    };
    httpd_uri_t updateUri = {
        .uri = "/api/v1/ota/update",
        .method = HTTP_POST,
        .handler = handlePostUpdate,
        .user_ctx = const_cast<ApiContext*>(context),
    };
    return httpd_register_uri_handler(server, &statusUri) == ESP_OK &&
           httpd_register_uri_handler(server, &updateUri) == ESP_OK;
}

esp_err_t OtaRoute::handleGetStatus(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    if (!requireOtaSecret(req, context, "")) {
        return ESP_OK;
    }

    const esp_app_desc_t* appDesc = esp_app_get_description();
    const esp_partition_t* running = esp_ota_get_running_partition();
    OtaService* ota = context->otaService;

    char body[512];
    snprintf(
        body,
        sizeof(body),
        "{\"v\":1,\"ok\":true,\"enabled\":true,\"state\":\"%s\",\"progress_percent\":%u,"
        "\"firmware_version\":\"%s\",\"target_version\":\"%s\",\"running_partition\":\"%s\","
        "\"last_error\":\"%s\",\"busy\":%s}",
        stateLabel(ota->state()),
        static_cast<unsigned>(ota->progressPercent()),
        appDesc != nullptr ? appDesc->version : "",
        ota->targetVersion(),
        running != nullptr ? running->label : "",
        ota->lastError(),
        ota->isSystemBusy() ? "true" : "false");
    return sendJsonResponse(req, 200, body);
}

esp_err_t OtaRoute::handlePostUpdate(httpd_req_t* req) {
    auto* context = static_cast<ApiContext*>(req->user_ctx);
    char body[kRequestBodyMax] = {};
    size_t bodyLen = 0;
    readBody(req, body, sizeof(body), bodyLen);

    char requestId[64] = {};
    bool force = false;
    char firmwareUrl[256] = {};

    if (bodyLen > 0) {
        cJSON* root = cJSON_Parse(body);
        if (root != nullptr) {
            const cJSON* requestIdNode = cJSON_GetObjectItemCaseSensitive(root, "request_id");
            const cJSON* forceNode = cJSON_GetObjectItemCaseSensitive(root, "force");
            const cJSON* urlNode = cJSON_GetObjectItemCaseSensitive(root, "url");
            if (cJSON_IsString(requestIdNode) && requestIdNode->valuestring != nullptr) {
                strncpy(requestId, requestIdNode->valuestring, sizeof(requestId) - 1);
            }
            if (cJSON_IsBool(forceNode)) {
                force = cJSON_IsTrue(forceNode);
            }
            if (cJSON_IsString(urlNode) && urlNode->valuestring != nullptr) {
                strncpy(firmwareUrl, urlNode->valuestring, sizeof(firmwareUrl) - 1);
            }
            cJSON_Delete(root);
        }
    }

    if (!requireOtaSecret(req, context, requestId)) {
        return ESP_OK;
    }

    OtaService* ota = context->otaService;
    if (ota->isSystemBusy()) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", "Device busy (audio active)", true, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    if (!ota->startUpdate(firmwareUrl[0] != '\0' ? firmwareUrl : nullptr, force)) {
        ErrorResponseFactory errors;
        char errBody[256] = {};
        errors.build("UNAVAILABLE", ota->lastError(), true, requestId, errBody, sizeof(errBody));
        return sendJsonResponse(req, 503, errBody);
    }

    char okBody[256];
    snprintf(
        okBody,
        sizeof(okBody),
        "{\"v\":1,\"ok\":true,\"state\":\"downloading\",\"request_id\":\"%s\"}",
        requestId);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_status(req, "202 Accepted");
    return httpd_resp_send(req, okBody, HTTPD_RESP_USE_STRLEN);
}
