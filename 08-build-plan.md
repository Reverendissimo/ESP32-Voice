# Pragmatic Build Plan

## Step 1

Create the project tree and empty classes exactly following the required layout.

Do not start by coding features in one place.

## Step 2

Implement these foundational classes first:
- `DeviceIdentity`
- `ConfigModels`
- `ConfigValidator`
- `ConfigStore`
- `ConfigManager`
- `WifiManager`
- `TimeSyncService`
- `HealthService`

## Step 3

Implement local REST server infrastructure:
- server bootstrap
- route registry
- error response factory
- health route
- version route
- config routes

## Step 4

Implement USB serial CLI with only the required commands.

## Step 5

Implement audio path:
- capture
- VAD
- upload service
- utterance state machine
- playback service

## Step 6

Implement display path:
- screen model
- screen parser
- renderer
- display route
- ui-event outbound client

## Step 7

Implement sensor services:
- battery
- environment
- radar
- alarm evaluator

## Step 8

Implement IR services.

## Step 9

Implement diagnostics hardening:
- recent log buffer
- metrics
- heartbeat
- retry/backoff polish
- richer status reporting

## Definition of acceptable output

At every stage:
- code compiles
- files remain small
- comments/docstrings are present
- responsibilities remain separated
- no monolithic shortcut is introduced
