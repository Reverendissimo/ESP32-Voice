# Cyberguard X Agent V2

A comprehensive system monitoring and security agent that collects detailed system information including processes, network interfaces, storage, USB devices, and process-level details like open files, sockets, and loaded libraries.

## Features

- **Real-time System Monitoring**: Collects comprehensive system data every 30 seconds
- **Process Analysis**: Detailed process information including CPU, memory, GPU usage, network I/O, disk I/O, **username identification**
- **Security Monitoring**: Tracks open files, open sockets, and loaded libraries for each process
- **Hardware Monitoring**: SMART disk health, USB devices, network interfaces
- **Web Dashboard**: Modern web interface with tree-like navigation and detailed process views
- **Data Compression**: Brotli compression for efficient data transmission (54% better than gzip)
- **Cross-platform**: Works on Linux, macOS, and Windows
- **Advanced Analysis**: Time-aware baseline establishment and suspicious process detection
- **Security Intelligence**: Process behavior analysis and anomaly detection
- **Baseline Management**: Web interface for starting and updating baseline scans per host
- **Time-aware Baselines**: Process behavior analysis considering hour of day, day of week, day of month, and day of year
- **Accurate Memory Reporting**: Fixed memory percentage calculation to show maximum process memory usage instead of sum of all percentages

## Architecture

The system consists of two main components:

1. **Agent (`agent0.py`)**: Collects system data and sends it to the server
2. **Web Server (`app.py`)**: Flask application that receives data and provides web interface

## Recent Updates

### Brotli Compression Implementation (Latest)

**Enhancement:** Upgraded data compression from gzip to Brotli for significantly better compression ratios and faster transmission.

**Implementation:**
- **Agent (`agent0.py`)**: Updated to use Brotli compression with `Content-Encoding: br` header
- **Server (`app.py`)**: Added Brotli decompression support alongside existing gzip support
- **Dependencies**: Added `brotli==1.1.0` to `requirements.txt`

**Performance Improvements:**
- **54% better compression** than gzip (0.18MB vs 0.39MB for 2.67MB data)
- **Faster compression/decompression** compared to gzip
- **Backward compatibility** maintained with gzip support
- **Reduced bandwidth usage** for all data transmissions

**Technical Details:**
- **Compression Ratio**: 6.6% vs gzip's 14.5% (7.9% improvement)
- **Network Savings**: 220KB per transmission (0.21MB saved)
- **Server Support**: Handles both `Content-Encoding: gzip` and `Content-Encoding: br`
- **Error Handling**: Proper error responses for invalid Brotli data

**Impact:**
- Reduced network bandwidth usage by 54%
- Faster data transmission to remote servers
- Improved scalability for high-frequency monitoring
- Maintained compatibility with existing gzip implementations

### Enhanced Process Alert Details

**Major Enhancement:** Enhanced all process-related alerts to include comprehensive process information for better security analysis and incident response.

**New Alert Structure:**
```json
{
  "id": "alert-uuid",
  "type": "NEW_PROCESS", 
  "severity": "WARNING",
  "description": "Unknown process 'process_name' detected",
  "recommendation": "Verify if this is authorized software",
  "process_name": "process_name",
  "process_exe": "/path/to/executable",
  "process_args": "command line arguments",
  "process_user": "username",
  "process_pid": 12345,
  "process_status": "sleeping",
  "process_hash": "hash_value",
  "process_arg_hash": "arg_hash_value",
  "cpu_percent": 2.5,
  "memory_percent": 1.2,
  "open_files": ["/path/to/file1", "/path/to/file2"],
  "loaded_libraries": ["/lib/libc.so.6", "/lib/libpthread.so.0"],
  "open_sockets": [
    {
      "fd": 15,
      "type": 1,
      "local_address": "127.0.0.1:1234",
      "remote_address": "8.8.8.8:53",
      "status": "ESTABLISHED"
    }
  ],
  "gpu_memory_used": 0,
  "gpu_utilization": 0,
  "network_sent_bytes": 1024,
  "network_recv_bytes": 2048,
  "network_connections": 5,
  "disk_read_bytes": 512,
  "disk_write_bytes": 256
}
```

**Features:**
- **Complete Process Information**: Includes executable path, arguments, PID, status, and user
- **Universal Coverage**: All process-related alerts (NEW_PROCESS, HEURISTIC_WARNING, HEURISTIC_CRITICAL, MALICIOUS_IP_CONNECTION, etc.) include detailed information
- **Resource Usage**: Real-time CPU, memory, GPU, and VRAM usage
- **File System Activity**: List of open files and loaded libraries
- **Network Activity**: Open sockets (both listening and connected), network I/O statistics
- **Disk Activity**: Read/write bytes for disk I/O monitoring
- **Error Handling**: Robust error handling with fallback to default values
- **Database Integration**: All detailed information stored in database for historical analysis

**Implementation:**
- **Enhanced Alert Creation**: NEW_PROCESS alerts now include detailed process information during creation
- **Comprehensive Data Retrieval**: Added `get_process_detailed_info()` function to fetch all process details
- **Real-time Enrichment**: Alerts are enriched with current process state when retrieved
- **Error Handling**: Robust error handling for data retrieval with fallback values
- **Database Integration**: All detailed information is properly stored and retrieved from database

**New Features:**
- **Process Details**: Executable path, arguments, user, PID, status
- **Resource Usage**: Current CPU, memory, GPU, VRAM usage
- **File Access**: List of open files for each process
- **Network Activity**: Open sockets, network I/O, connection count
- **Library Information**: Loaded libraries with file paths and hashes
- **Disk Activity**: Read/write bytes for disk I/O monitoring

**Security Benefits:**
- **Better Incident Response**: Detailed process information for faster analysis
- **Comprehensive Monitoring**: All process activities tracked in alerts
- **Resource Tracking**: Real-time resource usage for suspicious processes
- **Network Analysis**: Socket information for network-based threat detection

### Enhanced GPU/VRAM Structure with Error Handling

**Major Enhancement:** Updated agent0.py to provide enhanced GPU/VRAM monitoring with comprehensive error tracking for all data collection processes.

**New JSON Structure:**
```json
{
  "p_gpu": {
    "gpu_memory_used": 0,
    "gpu_utilization": 0,
    "gpu_error": 1
  },
  "p_gpu_error": 1,
  "p_cpu_error": 0,
  "p_mem_error": 0,
  "p_net_error": 0,
  "p_disk_error": 0,
  "p_files_error": 0,
  "p_sockets_error": 0,
  "p_loaded_libs_error": 0
}
```

**Implementation:**
- **Enhanced Error Tracking**: Each data type now has its own error field for data quality monitoring
- **GPU Error Handling**: Specific error tracking for GPU data collection with `gpu_error` field
- **Comprehensive Monitoring**: All process data types have error tracking
- **Database Schema Updates**: All error fields are properly stored in the database
- **Backward Compatibility**: Maintains compatibility with existing data structures

**Database Updates:**
- **`process_gpu`**: Added `gpu_error` field support
- **`processes`**: All error fields (`p_name_error`, `p_cpu_error`, etc.) now properly handled
- **`process_network`**: Added `net_io_error` field support
- **`process_disk`**: Added `disk_io_error` field support
- **`process_files`**: Added `open_files_error` field support
- **`process_sockets`**: Added `socket_error` field support
- **`process_libraries`**: Added `loaded_libs_error` field support

**New Features:**
- **Data Quality Monitoring**: Error fields help identify data collection issues
- **GPU Error Detection**: Specific tracking for GPU data collection problems
- **Enhanced Reliability**: Better error handling for all data collection processes
- **Multiple GPU Support**: Ready for systems with multiple GPUs (when available)

**Impact:**
- Improved data quality monitoring and error detection
- Better reliability for GPU-intensive monitoring scenarios
- Enhanced debugging capabilities for data collection issues
- Foundation for multiple GPU support in future updates

### GPU/VRAM Data Integration

**Enhancement:** All endpoints that return RAM data now consistently include CPU, GPU, and VRAM data for comprehensive resource monitoring.

**Implementation:**
- **Updated Endpoints**: Modified `analysis.py`, `adv_analytics.py`, and `suggestions.py`
- **GPU Monitoring**: Added GPU usage and GPU memory monitoring with configurable thresholds
- **Alert Integration**: GPU alerts now trigger security warnings and suggestions
- **Data Consistency**: All resource endpoints now return complete CPU/RAM/GPU/VRAM data

**New Features:**
- **GPU Usage Alerts**: Triggers when GPU utilization > 80% (WARNING severity)
- **GPU Memory Alerts**: Triggers when GPU memory usage > 90% (PROBLEM severity)
- **GPU Suggestions**: Performance suggestions now include GPU overload detection
- **Enhanced Analytics**: Advanced analytics now considers GPU usage in security scoring

**Endpoints Updated:**
- `GET /api/host_resource_totals` - Now returns `total_gpu_load` and `total_gpu_ram`
- `GET /api/hosts/` - Now includes `current_gpu_load` and `current_gpu_ram`
- `GET /api/hosts/<host_id>` - Now includes `total_gpu_load` and `total_gpu_ram`
- Advanced analytics alerts now include GPU resource monitoring

**Impact:**
- Complete resource monitoring (CPU, RAM, GPU, VRAM)
- Better detection of mining activities and GPU-intensive processes
- Enhanced security analysis with GPU resource consideration
- Consistent data format across all resource endpoints

### Host Data Management API

**Enhancement:** Added comprehensive host data management capabilities with secure deletion endpoints.

**Implementation:**
- **New Endpoint**: `DELETE /api/hosts/<hostname>/clear` for complete host data removal
- **Foreign Key Handling**: Proper deletion order to respect database constraints
- **Comprehensive Cleanup**: Removes all related data (scans, processes, baselines, alerts)
- **Safety Features**: Host existence validation and detailed deletion summary

**Features:**
- **Complete Data Removal**: Deletes all scans, processes, and related data for a host
- **Foreign Key Safety**: Properly handles all database constraints
- **Detailed Reporting**: Returns summary of deleted data (scans, processes, host_id)
- **Error Handling**: Proper error responses for missing hosts or database issues

**Data Removed:**
- All process data (GPU, network, disk, files, sockets, libraries)
- All scan data and related host information
- All baseline data and analytics
- All security alerts and metadata
- The host record itself

**Example Usage:**
```bash
curl -X DELETE "http://localhost:8000/api/hosts/Rev-X-000/clear"
```

**Impact:**
- Improved data management capabilities
- Secure and reliable host data cleanup
- Better database maintenance tools
- Foundation for advanced data management features

### Enhanced Suggestions System with Comprehensive Alert Management

**Major Enhancement:** Completely overhauled the suggestions system to provide comprehensive monitoring recommendations and improved alert storage capabilities.

**New Features:**

#### **Improved Baseline Suggestions**
- **INSUFFICIENT_BASELINE_DATA**: Detects hosts with <5 scans and recommends waiting for more data
- **UPDATE_BASELINE**: Detects hosts with 5+ scans but no baseline and recommends baseline creation
- **REFRESH_BASELINE**: Detects outdated baselines and recommends updates
- **Realistic Thresholds**: Reduced from 10 to 5 scans for baseline creation

#### **Enhanced Security Suggestions**
- **No Security Analysis Detection**: Identifies hosts with scan data but no security alerts
- **Unconfirmed Alert Detection**: Detects hosts with unconfirmed security alerts
- **Proactive Recommendations**: Suggests running security analysis when no alerts exist
- **Comprehensive Coverage**: Covers all security monitoring scenarios

#### **Improved Alert Storage System**
- **Fixed Alert Persistence**: Resolved issue where heuristic alerts weren't being stored in database
- **Enhanced Storage Function**: Updated `store_alerts_in_database()` to handle both database and heuristic alerts
- **Field Compatibility**: Fixed field mapping between heuristic alerts (`alert_type`) and database storage
- **Comprehensive Error Handling**: Better error handling for alert storage operations

#### **Offline Detection Improvements**
- **Realistic Threshold**: Changed offline detection from 5 minutes to 30 minutes
- **Reduced False Positives**: More appropriate for systems that don't require constant real-time monitoring
- **Better User Experience**: Less frequent false offline alerts

**Implementation Details:**

**Baseline Suggestions Logic:**
```python
# Check for hosts with insufficient baseline data (<5 scans)
# Check for hosts without baselines (5+ scans but no baseline)
# Check for outdated baselines (new scan data or age >30 days)
```

**Security Suggestions Logic:**
```python
# Check for hosts with scan data but no security alerts
# Check for unconfirmed security alerts
# Recommend security analysis when no alerts exist
```

**Alert Storage Fixes:**
- Fixed field mapping: `alert_type` vs `type`, `user_name` vs `user`
- Added storage call to `/alerts` endpoint
- Enhanced error handling and logging

**Impact:**
- **Comprehensive Monitoring**: All scenarios now have appropriate suggestions
- **Proactive Security**: Recommends security analysis when no alerts exist
- **Better Data Management**: Improved baseline and alert management
- **Reduced False Positives**: More realistic offline detection
- **Enhanced User Experience**: Clear, actionable recommendations for all situations

### Comprehensive Username Monitoring System

The system now includes advanced username monitoring for enhanced security:

**New User Detection (CRITICAL):**
- Detects when new users spawn processes never seen before
- Triggers CRITICAL alerts with -25 security score impact
- Alert: "New user 'username' running processes"
- Recommendation: "Investigate new user activity immediately"

**User Process Anomaly Detection (WARNING):**
- Identifies when users run processes they don't normally run
- Triggers WARNING alerts with -15 security score impact
- Alert: "User 'username' running unusual process 'process_name'"
- Recommendation: "Verify if this is authorized activity"

**Enhanced Security Suggestions:**
- User-related alerts prioritized with CRITICAL severity
- Immediate investigation recommendations
- Comprehensive coverage of user security scenarios
- Historical analysis against previous scans

**Baseline Integration:**
- Compares current users/processes against historical data
- Identifies unusual user behavior patterns
- Proactive monitoring for new user threats
- Comprehensive user activity tracking

### Memory Percentage Calculation Fix

**Issue:** The system was incorrectly summing memory percentages across all processes, leading to unrealistic values like 106% memory usage.

**Root Cause:** Memory percentages represent individual process usage relative to total system memory. Summing them creates meaningless values.

**Fix Applied:**
- Changed from `SUM(process_memory_percent)` to `MAX(process_memory_percent)` across all endpoints
- Updated 7 instances across 4 files: `adv_analytics.py`, `hosts.py`, `app.py`, `analysis.py`
- Now correctly reports the highest single process memory usage instead of the sum

**Impact:** 
- Realistic memory reporting (e.g., 22.4% instead of 106.4%)
- More accurate system resource monitoring
- Proper alert generation for memory thresholds

## Data Collection

### System Information Collected

#### Host Information
- Hostname
- Network interfaces with IP addresses
- Disk storage with SMART health data
- USB devices
- System token for identification

#### Process Information
For each running process, the agent collects:

- **Basic Info**: Name, PID, executable path, arguments, status, **username**
- **Resource Usage**: CPU percentage, memory percentage
- **GPU Information**: GPU utilization, memory usage (if available)
- **Network I/O**: Bytes sent/received, active connections
- **Disk I/O**: Bytes read/written
- **Open Files**: List of all open file handles
- **Open Sockets**: Network socket details (FD, local/remote addresses, status)
- **Loaded Libraries**: All dynamically loaded libraries and shared objects

## Baseline Analysis

The system includes advanced time-aware baseline analysis for security monitoring:

### Baseline Types
- **Hourly Baselines**: Process behavior patterns for each hour of the day (0-23)
- **Day of Week Baselines**: Patterns for each day of the week (Monday-Sunday)
- **Day of Month Baselines**: Patterns for each day of the month (1-31)
- **Day of Year Baselines**: Patterns for each day of the year (1-366)

### Baseline Metrics
For each process and time period, the system calculates:
- Average CPU usage
- Average memory usage
- Average GPU utilization and memory
- Average network I/O (sent/recv bytes, connections)
- Average disk I/O (read/write bytes)
- Average counts of open files, sockets, and loaded libraries
- Total occurrences and last updated timestamp

### Baseline Management
- **Web Interface**: Dropdown menu to select hosts and start/update baseline scans
- **Automatic Calculation**: Baselines are calculated from historical scan data
- **Real-time Comparison**: Current processes are compared against established baselines
- **Anomaly Detection**: Identifies processes that deviate significantly from baseline patterns

### Baseline Calculation with Progress Tracking

The system now includes progress tracking for baseline calculations:

**Start Calculation:**
```bash
curl "http://localhost:8000/baseline/process"
```
**Response:**
```json
{
    "status": "started",
    "message": "Baseline calculation started in background",
    "progress_endpoint": "/baseline/process/progress"
}
```

**Check Progress:**
```bash
curl "http://localhost:8000/baseline/process/progress"
```
**Response:**
```json
{
    "status": "running",
    "progress": 16,
    "current_host": "Processing Rev-X-000... (56/169 processes) - Analyzing libraries",
    "processed_hosts": 0,
    "total_hosts": 2,
    "processed_processes": 56,
    "start_time": "2025-07-25T20:17:43.889725"
}
```

**Progress States:**
- `idle`: No calculation in progress
- `running`: Calculation is active
- `completed`: Calculation finished successfully
- `error`: Calculation failed with error message

**Features:**
- **Real-time Progress**: Percentage completion and current host being processed
- **Granular Updates**: Progress updates every 5 processes with detailed status
- **Phase Tracking**: Shows current analysis phase (sockets, files, libraries)
- **Process Counting**: Real-time count of processed processes
- **Background Processing**: Non-blocking calculation that runs in background
- **Status Tracking**: Detailed progress information including start/end times
- **Error Handling**: Clear error messages if calculation fails

## JSON Data Structure

The agent sends data in the following JSON structure:

```json
{
  "host_info": {
    "hostname": "system-hostname",
    "token": "unique-system-token",
    "interfaces": [
      {
        "name": "eth0",
        "addresses": [
          {"type": "ipv4", "address": "192.168.1.100"},
          {"type": "ipv6", "address": "fe80::1234:5678:9abc:def0"}
        ],
        "addresses_error": 0
      }
    ],
    "disks": [
      {
        "device": "/dev/sda",
        "mountpoint": "/",
        "fstype": "ext4",
        "total": 250000000000,
        "used": 100000000000,
        "free": 150000000000,
        "percent": 40.0,
        "disk_error": 0,
        "smart": {
          "Temperature_Celsius": "35",
          "Power_On_Hours": "1234",
          "smart_error": 0
        }
      }
    ],
    "usb_devices": [
      "USB Device: Logitech USB Keyboard"
    ],
    "usb_devices_error": 0
  },
  "process_list": [
    {
      "p_name": "chrome",
      "p_pid": 12345,
      "p_exe": "/opt/google/chrome/chrome",
      "p_args": ["--no-sandbox", "--disable-dev-shm-usage"],
      "p_cpu": 2.5,
      "p_mem": 15.3,
      "p_status": "sleeping",
      "p_user": "user",
      "p_name_hash": 12345678901234567890,
      "p_gpu": {
        "gpu_memory_used": 0,
        "gpu_utilization": 0,
        "gpu_error": 1
      },
      "p_gpu_error": 1,
      "p_cpu_error": 0,
      "p_mem_error": 0,
      "p_net_error": 0,
      "p_disk_error": 0,
      "p_files_error": 0,
      "p_sockets_error": 0,
      "p_loaded_libs_error": 0,
      "p_arg_hash": 98765432109876543210,
      "p_net": {
        "sent_bytes": 1024000,
        "recv_bytes": 2048000,
        "connections": 12,
        "net_io_error": 0
      },
      "p_disk": {
        "read_bytes": 512000,
        "write_bytes": 256000,
        "disk_io_error": 0
      },
      "p_files": [
        "/home/user/.config/chrome/Default/Cookies",
        "/dev/shm/.com.google.Chrome.abc123",
        "/usr/lib/x86_64-linux-gnu/libc.so.6"
      ],
      "p_sockets": [
        {
          "fd": 15,
          "type": 1,
          "local": "192.168.1.100:54321",
          "remote": "8.8.8.8:53",
          "status": "ESTABLISHED"
        }
      ],
      "p_loaded_libs": [
        "/usr/lib/x86_64-linux-gnu/libc.so.6",
        "/usr/lib/x86_64-linux-gnu/libpthread.so.0",
        "/opt/google/chrome/chrome"
      ],
      "p_loaded_libs_error": 0,
      "p_user_error": 0
    }
  ]
}
```

## API Endpoints (Implemented)

# Each endpoint below is described with:
# - HTTP method and path
# - Purpose/description
# - Input parameters (required/optional, type, format)
# - Request body format (if applicable)
# - Response format (all fields, types, and example values)
# - Error responses (status codes, error message format)
# - Example requests and responses

| Method | Path                                 | Description                                      |
|--------|--------------------------------------|--------------------------------------------------|
| GET    | /                                   | Main dashboard page (HTML)                       |
| GET    | /scan_details/<scan_id>             | Detailed scan view page (HTML)                   |
| GET    | /test                               | Test page for API endpoints (HTML)               |
| GET    | /api/health                         | Server health check (JSON)                       |
| POST   | /api/save_data                      | Receive agent data (gzip JSON or JSON)           |
| GET    | /api/stats                          | System statistics (JSON)                         |
| GET    | /api/data                           | Aggregated data for all hosts (JSON)             |
| GET    | /baseline/process                   | Compute and store process baselines (JSON)       |
| GET    | /baseline/process/details           | Get process baseline details (JSON)              |
| GET    | /api/hosts/                         | Get all hosts with metadata and latest scan info |
| GET    | /api/hosts/<host_id>                | Get detailed information about a specific host   |
| GET/PUT| /api/hosts/<host_id>/metadata      | Get or update host metadata                      |
| GET    | /api/hosts/<host_id>/scans          | Get all scans for a host with pagination         |
| GET    | /api/hosts/<host_id>/stats          | Get comprehensive statistics for a host          |
| GET    | /api/hosts/<host_id>/processes      | Get all processes for a host with filtering      |
| GET    | /api/hosts/<token>/alerts           | Get alerts for a host based on token auth        |
| GET    | /api/hosts/<host_id>/process/<process_name_hash> | Get detailed process information on a host |
| GET    | /api/hosts/<token>/process-history  | Get process history for a host/token             |
| GET    | /api/hosts/<host_id>/logged-users   | Get logged users for a specific host             |
| GET    | /api/total_processes                | Get total process count with optional host filter |
| GET    | /api/host_resource_totals           | Get total CPU, RAM, GPU load, and GPU memory usage for a specific host |
| GET    | /api/suggestions                    | Get real-time actionable suggestions for frontend |

## Threat Intelligence API Endpoints

| Method | Path                                 | Description                                      |
|--------|--------------------------------------|--------------------------------------------------|
| GET    | /api/threat-intel/blocklists        | Get all blocklist configurations                  |
| POST   | /api/threat-intel/blocklists        | Add new blocklist configuration                  |
| GET    | /api/threat-intel/blocklists/{name} | Get specific blocklist configuration and stats   |
| GET    | /api/threat-intel/blocklists/{name}/status | Get blocklist update status and statistics |
| GET    | /api/threat-intel/check/{ip}        | Check if IP is in any blocklist                 |
| GET    | /api/threat-intel/stats             | Get overall threat intelligence statistics       |


## Advanced Analytics API Endpoints

| Method | Path                                 | Description                                      |
|--------|--------------------------------------|--------------------------------------------------|
| GET    | /adv-analytics/hosts/<host_id>/baseline-quality | Check baseline quality for security analysis |
| GET    | /adv-analytics/hosts/<host_id>/status | Get security and health status for a host |
| GET    | /adv-analytics/hosts/<host_id>/alerts | Get all active security alerts for a host |
| POST   | /adv-analytics/hosts/<host_id>/alerts/<alert_id>/confirm | Confirm or deny a security alert |
| GET    | /adv-analytics/hosts/<host_id>/trusted-processes | Get trusted processes for a host |
| POST   | /adv-analytics/hosts/<host_id>/trusted-processes | Add a trusted process for a host |
| GET    | /adv-analytics/hosts/<host_id>/learning-stats | Get learning statistics for a host |

## Admin API Endpoints

| Method | Path                                 | Description                                      |
|--------|--------------------------------------|--------------------------------------------------|
| GET    | /api/admin/users                     | List all users                                   |
| POST   | /api/admin/users                     | Create a new user                                |
| GET    | /api/admin/users/<username>          | Get user information                             |
| PUT    | /api/admin/users/<username>          | Update user information                          |
| DELETE | /api/admin/users/<username>          | Delete a user                                    |
| GET    | /api/admin/devices                   | List all devices (hosts) with adoption status   |
| POST   | /api/admin/devices/<host_id>/adopt   | Adopt a device (host)                           |
| PUT    | /api/admin/devices/<host_id>         | Update device metadata                           |
| DELETE | /api/admin/devices/<host_id>         | Remove device metadata                           |

# For each endpoint, see the detailed documentation below.

---

### `GET /`
- **Description**: Main dashboard page.
- **Arguments**: None
- **Response**: HTML page

### `GET /scan_details/<scan_id>`
- **Description**: Detailed scan view page.
- **Arguments**:
  - `scan_id` (required, int): ID of the scan to view
- **Response**: HTML page

### `GET /test`
- **Description**: Test page for API endpoints, parametrized with host and port.
- **Arguments**: None
- **Response**: HTML page

### `GET /api/health`
- **Description**: Health check endpoint.
- **Arguments**: None
- **Response Example**:
```json
{
  "status": "healthy",
  "service": "Cyberguard X Agent V2"
}
```

### `POST /api/save_data`
- **Description**: Save scan data to database. Accepts JSON, gzip-compressed JSON, or Brotli-compressed JSON.
- **Arguments**:
  - Body: JSON data (see agent data structure above), gzip-compressed JSON (with header `Content-Encoding: gzip`), or Brotli-compressed JSON (with header `Content-Encoding: br`)
- **Response Example (success)**:
```json
{
  "status": "success",
  "message": "Data saved successfully"
}
```
- **Response Example (error)**:
```json
{
  "error": "No data provided"
}
```
- **Compression Support**:
  - **Brotli** (`Content-Encoding: br`): 54% better compression than gzip, recommended for production
  - **Gzip** (`Content-Encoding: gzip`): Standard compression, backward compatible
  - **Uncompressed**: Plain JSON data (no Content-Encoding header)

### `GET /api/stats`
- **Description**: Get comprehensive system statistics.
- **Arguments**: None
- **Response Example**:
```json
{
  "overview": {
    "total_hosts": 3,
    "total_scans": 120,
    "total_processes": 450,
    "total_disks": 5,
    "total_interfaces": 8
  },
  "recent_activity": {
    "scans_last_hour": 10,
    "processes_last_hour": 40
  },
  "top_processes": [
    {
      "name": "python3",
      "cpu_percent": 12.5,
      "memory_percent": 5.2,
      "hostname": "host1",
      "timestamp": 1712345678
    }
  ]
}
```

### `GET /api/data`
- **Description**: Returns high-level, aggregated data for all hosts, ideal for dashboards or visualization maps. For each host, includes only the latest scan and aggregates total CPU, GPU, memory, and GPU utilization. Also includes map coordinates (x, y), adoption status, alias from host metadata, and logged user information.
- **Arguments**: None
- **Response Example**:
```json
[
  {
    "adopted": 1,
    "alias": "Rev-X-000",
    "hostname": "Rev-X-000",
    "timestamp": 1752605016,
    "token": "00000000000000000000000000000000",
    "totalCpu": 34.1,
    "totalGpu": 0,
    "totalGpuUtilization": 0,
    "totalMem": 23.51079681,
    "x": 50,
    "y": 100,
    "loggedUsers": 6,
    "uniqueUsers": 2
  }
]
```
- **Field Explanations:**
  - `adopted` (int/bool): Whether the host is adopted (1/true = adopted, 0/false = not adopted).
  - `alias` (string): Custom alias or display name for the host, if set.
  - `hostname` (string): The system hostname of the host.
  - `timestamp` (int): UNIX timestamp of the latest scan for this host.
  - `token` (string): Unique token associated with the host (for authentication or identification).
  - `totalCpu` (float): Sum of CPU usage percentages for all processes in the latest scan.
  - `totalGpu` (int): Sum of GPU memory used (MB or bytes, depending on agent reporting) for all processes in the latest scan.
  - `totalGpuUtilization` (int): Sum of GPU utilization percentages for all processes in the latest scan.
  - `totalMem` (float): Maximum memory usage percentage among all processes in the latest scan.
  - `x` (int): X coordinate for map or dashboard visualization (from host metadata).
  - `y` (int): Y coordinate for map or dashboard visualization (from host metadata).
  - `loggedUsers` (int): Total number of active user sessions from the latest scan.
  - `uniqueUsers` (int): Number of unique usernames logged in from the latest scan.

### `GET /baseline/process`
- **Description**: Compute and store process baselines from all historical data.
- **Arguments**: None
- **Response Example**:
```json
{
  "status": "success",
  "processed_hosts": 3,
  "processed_processes": 120
}
```

### `GET /baseline/process/details`
- **Description**: Returns baseline details for a process hash (and optionally argument hash).
- **Arguments**:
  - `process_name_hash` (required, str): Hash of the process name
  - `process_arg_hash` (optional, str): Hash of the process arguments
- **Response Example**:
```json
{
  "status": "success",
  "process_name_hash": "abc123",
  "process_arg_hash": "def456",
  "numerical": [
    {
      "process_name_hash": "abc123",
      "process_arg_hash": "def456",
      "avg_cpu_percent": 2.5,
      "avg_memory_percent": 1.2,
      "avg_gpu_memory_used": 0,
      "avg_gpu_utilization": 0,
      "avg_sent_bytes": 1000,
      "avg_recv_bytes": 2000,
      "avg_connections": 1,
      "avg_read_bytes": 500,
      "avg_write_bytes": 250,
      "total_samples": 10
    }
  ],
  "open_sockets": [
    {"remote_ip": "8.8.8.8", "count": 2}
  ],
  "open_files": [
    {"file_path": "/tmp/file.txt", "count": 1}
  ],
  "file_extensions": [
    {"file_extension": ".txt", "count": 1}
  ],
  "loaded_libraries": [
    {"library_path": "/usr/lib/libc.so.6", "count": 1}
  ],
  "library_filenames": [
    {"library_filename": "libc.so.6", "count": 1}
  ]
}
```

## Suspicious Process Detection API

### Endpoint
```
```

### `GET /api/hosts/:token/process-history`
- **Description**: Retrieve the full historical metrics for a specific process instance on a host, uniquely identified by a combination of hashed name and hashed arguments.
- **Input Parameters:**
  - `token` (URL param, string): The unique identifier for the host (from the scans table).
  - `p_name_hash` (query param, string): Hash of the process name (required).
  - `p_arg_hash` (query param, string): Hash of the process arguments (required).
- **Error Responses:**
  - `400 Bad Request`: If `p_name_hash` or `p_arg_hash` is missing.
  - `404 Not Found`: If the token does not map to a valid host.
- **Success Response (200):**
  - A JSON array of objects, each representing a process instance at a point in time (chronologically sorted by scan timestamp).
- **Example Output:**
```json
[
  {
    "id": 1,
    "scan_id": 1,
    "scan_timestamp": 1752603461,
    "process_name": "systemd",
    "process_pid": 1,
    "process_exe": "/usr/lib/systemd/systemd",
    "process_args": "/sbin/init",
    "process_cpu_percent": 0.0,
    "process_memory_percent": 0.0183105,
    "process_status": "sleeping",
    "process_name_hash": "8309794303940435839218678809263048060",
    "process_arg_hash": "283566028735129525850497257655948298227",
    "process_user": "root",
    "p_name_error": 0,
    "p_pid_error": 0,
    "p_exe_error": 0,
    "p_args_error": 0,
    "p_cpu_error": 0,
    "p_mem_error": 0,
    "p_status_error": 0,
    "p_name_hash_error": 0,
    "p_arg_hash_error": 0,
    "p_user_error": 0
  },
  ...
]
```
- **Field Explanations:**
  - `id` (int): Unique process record ID.
  - `scan_id` (int): The scan this process record belongs to.
  - `scan_timestamp` (int): UNIX timestamp of the scan.
  - `process_name` (string): Name of the process.
  - `process_pid` (int): Process ID at the time of scan.
  - `process_exe` (string): Path to the process executable.
  - `process_args` (string): Command-line arguments for the process.
  - `process_cpu_percent` (float): CPU usage percentage for the process at the time of scan.
  - `process_memory_percent` (float): Memory usage percentage for the process at the time of scan.
  - `process_status` (string): Status of the process (e.g., running, sleeping).
  - `process_name_hash` (string): Hash of the process name (used for identification).
  - `process_arg_hash` (string): Hash of the process arguments (used for identification).
  - `process_user` (string): Username under which the process is running.
  - `p_name_error`, `p_pid_error`, `p_exe_error`, `p_args_error`, `p_cpu_error`, `p_mem_error`, `p_status_error`, `p_name_hash_error`, `p_arg_hash_error`, `p_user_error` (int): Error flags for each field (0 = no error, 1 = error collecting this field).

This endpoint is optimized for time series analysis and visualization of process behavior over time for a specific process instance on a given host.

### `GET /api/hosts/<host_id>/logged-users`
- **Description**: Get detailed logged user information for a specific host. Returns all currently logged users with session details including username, terminal, login time, and remote host information.
- **Input Parameters:**
  - `host_id` (required, int): ID of the host to retrieve logged users for (e.g., 1, 2, 3)
  - `scan_id` (optional, int): Filter by specific scan ID (e.g., 13, 25, 100). If not provided, returns data from the latest scan.
- **Example Usage**:
```bash
# Get logged users for host ID 1 (latest scan)
curl -X GET "http://localhost:8000/api/hosts/1/logged-users"

# Get logged users for host ID 2 with jq formatting
curl -X GET "http://localhost:8000/api/hosts/2/logged-users" | jq

# Get logged users from specific scan
curl -X GET "http://localhost:8000/api/hosts/2/logged-users?scan_id=67"

# Get logged users and save to file
curl -X GET "http://localhost:8000/api/hosts/2/logged-users" > logged_users.json
```
- **Response Example**:
```json
{
  "host_id": 2,
  "hostname": "vps-73fbb828",
  "scan_id": 67,
  "scan_timestamp": 1753342555,
  "token": "00000000000000000000000000000001",
  "logged_users": [
    {
      "id": 1,
      "username": "rev",
      "terminal": "pts/0",
      "login_time": "Jul 19 17:30 (31.61.227.62)",
      "remote_host": "(31.61.227.62)",
      "session_name": "",
      "session_id": "",
      "state": "",
      "idle_time": "",
      "logged_users_error": 0
    },
    {
      "id": 2,
      "username": "dado",
      "terminal": "pts/6",
      "login_time": "Jul 19 19:10 (tmux(604164).%0)",
      "remote_host": "(tmux(604164).%0)",
      "session_name": "",
      "session_id": "",
      "state": "",
      "idle_time": "",
      "logged_users_error": 0
    }
  ],
  "total_users": 6,
  "unique_usernames": 2
}
```
- **Field Explanations:**
  - `host_id` (int): Host identifier
  - `hostname` (string): System hostname
  - `scan_id` (int): ID of the scan this data comes from
  - `scan_timestamp` (int): UNIX timestamp of the scan
  - `token` (string): Authentication token for the host
  - `logged_users` (array): List of currently logged users
    - `id` (int): Unique logged user record ID
    - `username` (string): Username of the logged user
    - `terminal` (string): Terminal device (e.g., "pts/0", "pts/6")
    - `login_time` (string): Login time and source (e.g., "Jul 19 17:30 (31.61.227.62)")
    - `remote_host` (string): Remote host information (e.g., "(31.61.227.62)", "(tmux(604164).%0)")
    - `session_name` (string): Session name (if available)
    - `session_id` (string): Session ID (if available)
    - `state` (string): Session state (if available)
    - `idle_time` (string): Idle time (if available)
    - `logged_users_error` (int): Error flag for user data collection (0 = success, 1 = error)
  - `total_users` (int): Total number of active user sessions
  - `unique_usernames` (int): Number of unique usernames logged in
- **Error Responses:**
  - `404 Not Found`: Host not found for the specified host_id
  - `404 Not Found`: No scan data available for the specified scan_id
  - `500 Internal Server Error`: Server error with detailed error message

## Host Management API

### `GET /api/hosts/`
- **Description**: Get all hosts with their metadata and latest scan information.
- **Input Parameters**: None
- **Example Usage**:
```bash
# Get all hosts
curl -X GET "http://localhost:8000/api/hosts/"

# Get all hosts with jq formatting
curl -X GET "http://localhost:8000/api/hosts/" | jq
```
- **Response Example**:
```json
{
  "hosts": [
    {
      "id": 1,
      "hostname": "Rev-X-000",
      "created_at": "Tue, 15 Jul 2025 18:17:41 GMT",
      "updated_at": "Tue, 15 Jul 2025 18:43:36 GMT",
      "x": 36,
      "y": 44,
      "adopted": null,
      "alias": null,
      "total_scans": 13,
      "last_scan_timestamp": 1752605016,
      "current_token": "00000000000000000000000000000000",
      "current_cpu": 34.1,
      "current_ram": 23.51,
      "current_gpu_load": 0.0,
      "current_gpu_ram": 0.0,
      "last_scan_id": 13
    }
  ],
  "total_hosts": 1
}
```
- **Field Explanations**:
  - `id` (int): Unique host identifier
  - `hostname` (string): System hostname
  - `created_at` (string): Host creation timestamp
  - `updated_at` (string): Last update timestamp
  - `x`, `y` (int): Position coordinates for visualization
  - `adopted` (int): Adoption status (1 = adopted, 0 = not adopted)
  - `alias` (string): Custom display name for the host
  - `total_scans` (int): Total number of scans for this host
  - `last_scan_timestamp` (int): UNIX timestamp of the most recent scan
  - `current_token` (string): Current authentication token for the host
  - `current_cpu` (float): Total CPU usage percentage from latest scan
  - `current_ram` (float): Total RAM usage percentage from latest scan
  - `current_gpu_load` (float): Total GPU utilization percentage from latest scan
  - `current_gpu_ram` (float): Total GPU memory usage from latest scan
  - `last_scan_id` (int): ID of the most recent scan

### `GET /api/hosts/<host_id>`
- **Description**: Get detailed information about a specific host.
- **Input Parameters**:
  - `host_id` (required, int): ID of the host to retrieve (e.g., 1, 2, 3)
- **Example Usage**:
```bash
# Get details for host ID 1
curl -X GET "http://localhost:8000/api/hosts/1"

# Get details for host ID 2 with jq formatting
curl -X GET "http://localhost:8000/api/hosts/2" | jq

# Get details for host ID 3 and save to file
curl -X GET "http://localhost:8000/api/hosts/3" > host_details.json
```
- **Response Example**:
```json
{
  "host": {
    "id": 1,
    "hostname": "Rev-X-000",
    "hostname_error": 0,
    "created_at": "Tue, 15 Jul 2025 18:17:41 GMT",
    "updated_at": "Tue, 15 Jul 2025 18:43:36 GMT",
    "x": 75,
    "y": 125,
    "adopted": 1,
    "alias": "Rev-X-000-Updated"
  },
  "scan_statistics": {
    "total_scans": 13,
    "first_scan": 1752603461,
    "last_scan": 1752605016,
    "avg_scan_interval": "1752604349.9231"
  },
  "latest_scan": {
    "id": 13,
    "host_id": 1,
    "scan_timestamp": 1752605016,
    "token": "00000000000000000000000000000000",
    "total_processes": 133,
    "total_cpu": 34.10000003129244,
    "total_memory": 23.510801792144775,
    "total_gpu_load": "0",
    "total_gpu_ram": "0"
  },
  "hardware": {
    "interfaces": 4,
    "disks": 1
  },
  "logged_users": {
    "total_sessions": 6,
    "unique_users": 2
  }
}
```
- **Field Explanations**:
  - `host` (object): Basic host information with metadata
  - `scan_statistics` (object): Historical scan data summary
  - `latest_scan` (object): Most recent scan details with aggregated metrics (includes GPU usage)
  - `hardware` (object): Hardware component counts from latest scan
  - `logged_users` (object): Summary of currently logged users from latest scan
    - `total_sessions` (int): Total number of active user sessions
    - `unique_users` (int): Number of unique usernames logged in

### `GET /api/hosts/<host_id>/metadata`
- **Description**: Get host metadata (position, adoption status, alias).
- **Input Parameters**:
  - `host_id` (required, int): ID of the host (e.g., 1, 2, 3)
- **Example Usage**:
```bash
# Get metadata for host ID 1
curl -X GET "http://localhost:8000/api/hosts/1/metadata"

# Get metadata for host ID 2 with jq formatting
curl -X GET "http://localhost:8000/api/hosts/2/metadata" | jq
```
- **Response Example**:
```json
{
  "host_id": 1,
  "metadata": {
    "x": 75,
    "y": 125,
    "adopted": 1,
    "alias": "Rev-X-000-Updated"
  }
}
```

### `PUT /api/hosts/<host_id>/metadata`
- **Description**: Update host metadata (position, adoption status, alias).
- **Input Parameters**:
  - `host_id` (required, int): ID of the host (e.g., 1, 2, 3)
  - Body (JSON): Metadata to update
- **Example Usage**:
```bash
# Update metadata for host ID 1
curl -X PUT "http://localhost:8000/api/hosts/1/metadata" \
  -H "Content-Type: application/json" \
  -d '{
    "x": 75,
    "y": 125,
    "adopted": 1,
    "alias": "Rev-X-000-Updated"
  }'

# Update only position for host ID 2
curl -X PUT "http://localhost:8000/api/hosts/2/metadata" \
  -H "Content-Type: application/json" \
  -d '{
    "x": 100,
    "y": 200
  }'

# Update only alias for host ID 3
curl -X PUT "http://localhost:8000/api/hosts/3/metadata" \
  -H "Content-Type: application/json" \
  -d '{
    "alias": "New-Host-Alias"
  }'
```
```json
{
  "x": 75,
  "y": 125,
  "adopted": 1,
  "alias": "Rev-X-000-Updated"
}
```
- **Response Example**:
```json
{
  "message": "Host metadata updated successfully",
  "host_id": 1,
  "metadata": {
    "x": 75,
    "y": 125,
    "adopted": 1,
    "alias": "Rev-X-000-Updated"
  }
}
```

### `GET /api/hosts/<host_id>/scans`
- **Description**: Get all scans for a specific host with pagination.
- **Input Parameters**:
  - `host_id` (required, int): ID of the host (e.g., 1, 2, 3)
  - `page` (optional, int): Page number (default: 1)
  - `per_page` (optional, int): Items per page (default: 50, max: 1000)
- **Example Usage**:
```bash
# Get all scans for host ID 1 (first page)
curl -X GET "http://localhost:8000/api/hosts/1/scans"

# Get scans for host ID 2 with custom pagination
curl -X GET "http://localhost:8000/api/hosts/2/scans?page=2&per_page=10"

# Get all scans for host ID 3 with jq formatting
curl -X GET "http://localhost:8000/api/hosts/3/scans" | jq

# Get first 5 scans for host ID 1
curl -X GET "http://localhost:8000/api/hosts/1/scans?per_page=5"
```
- **Response Example**:
```json
{
  "hostname": "Rev-X-000",
  "host_id": 1,
  "scans": [
    {
      "id": 13,
      "host_id": 1,
      "scan_timestamp": 1752605016,
      "token": "00000000000000000000000000000000",
      "process_count": 133,
      "total_cpu": 34.10000003129244,
      "total_memory": 23.510801792144775
    }
  ],
  "pagination": {
    "page": 1,
    "per_page": 50,
    "total": 13,
    "pages": 1
  }
}
```

### `GET /api/hosts/<host_id>/stats`
- **Description**: Get comprehensive statistics for a specific host.
- **Input Parameters**:
  - `host_id` (required, int): ID of the host (e.g., 1, 2, 3)
- **Example Usage**:
```bash
# Get statistics for host ID 1
curl -X GET "http://localhost:8000/api/hosts/1/stats"

# Get statistics for host ID 2 with jq formatting
curl -X GET "http://localhost:8000/api/hosts/2/stats" | jq

# Get statistics for host ID 3 and save to file
curl -X GET "http://localhost:8000/api/hosts/3/stats" > host_stats.json
```
- **Response Example**:
```json
{
  "hostname": "Rev-X-000",
  "host_id": 1,
  "scan_statistics": {
    "total_scans": 13,
    "first_scan": 1752603461,
    "last_scan": 1752605016
  },
  "process_statistics": {
    "unique_processes": 56,
    "total_process_records": 1737,
    "avg_cpu": 0.2500287856505599,
    "avg_memory": 0.1745370894790588,
    "max_cpu": 16.8,
    "max_memory": 1.91373
  },
  "recent_activity": {
    "recent_scans": 0
  },
  "top_processes": [
    {
      "process_name": "cursor",
      "process_cpu_percent": 16.8,
      "process_memory_percent": 1.90644,
      "scan_timestamp": 1752604157
    }
  ]
}
```

### `GET /api/hosts/<host_id>/processes`
- **Description**: Get all processes for a specific host with filtering options and process grouping.
- **Input Parameters**:
  - `host_id` (required, int): ID of the host (e.g., 1, 2, 3)
  - `scan_id` (optional, int): Filter by specific scan (e.g., 13, 25, 100)
  - `process_name` (optional, string): Filter by process name (partial match) (e.g., "cursor", "python", "bash")
  - `min_cpu` (optional, float): Filter by minimum CPU usage (e.g., 1.0, 5.5, 10.0)
  - `min_memory` (optional, float): Filter by minimum memory usage (e.g., 0.5, 1.0, 5.0)
  - `page` (optional, int): Page number (default: 1)
  - `per_page` (optional, int): Items per page (default: 100, max: 1000)
  - `group_by_name` (optional, bool, default: true): Group processes by name to eliminate duplicates
- **Example Usage**:
```bash
# Get all processes for host ID 1
curl -X GET "http://localhost:8000/api/hosts/1/processes"

# Get processes for host ID 2 from specific scan
curl -X GET "http://localhost:8000/api/hosts/2/processes?scan_id=13"

# Get cursor processes for host ID 1
curl -X GET "http://localhost:8000/api/hosts/1/processes?process_name=cursor"

# Get high CPU processes for host ID 2
curl -X GET "http://localhost:8000/api/hosts/2/processes?min_cpu=5.0"

# Get high memory processes for host ID 3
curl -X GET "http://localhost:8000/api/hosts/3/processes?min_memory=1.0"

# Get cursor processes with high CPU from specific scan
curl -X GET "http://localhost:8000/api/hosts/1/processes?scan_id=13&process_name=cursor&min_cpu=5.0"

# Get processes with custom pagination
curl -X GET "http://localhost:8000/api/hosts/1/processes?page=2&per_page=50"

# Get all processes with jq formatting
curl -X GET "http://localhost:8000/api/hosts/1/processes" | jq

# Get unique processes (default grouping)
curl -X GET "http://localhost:8000/api/hosts/1/processes?group_by_name=true"

# Get all process instances (no grouping)
curl -X GET "http://localhost:8000/api/hosts/1/processes?group_by_name=false"
- **Response Example**:
```json
{
  "hostname": "Rev-X-000",
  "host_id": 1,
  "processes": [
    {
      "id": 625,
      "scan_id": 5,
      "scan_timestamp": 1752604157,
      "process_name": "cursor",
      "process_pid": 836175,
      "process_exe": "/tmp/.mount_cursorVE93fW/usr/share/cursor/cursor",
      "process_args": "/tmp/.mount_cursorVE93fW/usr/share/cursor/cursor --type=renderer...",
      "process_cpu_percent": 16.8,
      "process_memory_percent": 1.90644,
      "process_status": "sleeping",
      "process_name_hash": "217960999866174382285787378220915670545",
      "process_arg_hash": "60593947995519878940892087288164047350",
      "p_name_error": 0,
      "p_pid_error": 0,
      "p_exe_error": 0,
      "p_args_error": 0,
      "p_cpu_error": 0,
      "p_mem_error": 0,
      "p_status_error": 0,
      "p_name_hash_error": 0,
      "p_arg_hash_error": 0
    }
  ],
  "grouped_by_name": true,
  "pagination": {
    "page": 1,
    "per_page": 100,
    "total": 56,
    "pages": 1
  }
}
```

**Note**: When `group_by_name=true` (default), the response includes aggregated metrics:
- `max_cpu_percent`: Highest CPU usage observed
- `avg_cpu_percent`: Average CPU usage across all scans  
- `max_memory_percent`: Highest memory usage observed
- `avg_memory_percent`: Average memory usage across all scans
- `occurrences`: How many times this process was seen
- `first_seen`: Timestamp of first occurrence
- `last_seen`: Timestamp of most recent occurrence

### When `group_by_name=false`:
- Returns all process instances (original behavior)
- Each record represents a specific process at a specific scan time

### Example Usage:
```bash
# Get unique processes (default)
curl "http://localhost:8000/api/hosts/1/processes?group_by_name=true"

# Get all process instances
curl "http://localhost:8000/api/hosts/1/processes?group_by_name=false"
```
- **Error Example**:
```json
{"status": "error", "message": "Host not found"}
```
- **Example curl**:
```bash
curl -X DELETE http://localhost:8000/api/admin/devices/1
```

---

## Threat Intelligence System

The threat intelligence system provides IP blocklist management and malicious IP detection capabilities.

### `GET /api/threat-intel/blocklists`
- **Description**: Get all blocklist configurations.
- **Arguments**: None
- **Response Example**:
```json
{
  "status": "success",
  "blocklists": [
    {
      "id": 1,
      "name": "firehol_level1",
      "display_name": "FireHOL Level 1",
      "source_url": "https://raw.githubusercontent.com/firehol/blocklist-ipsets/master/firehol_level1.netset",
      "category": "malware",
      "threat_level": 1,
      "update_frequency": "daily",
      "is_active": true,
      "last_updated": null,
      "last_successful_update": null,
      "description": "FireHOL Level 1 blocklist - maximum protection with minimum false positives"
    }
  ],
  "total": 1
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/blocklists"
```

### `POST /api/threat-intel/blocklists`
- **Description**: Add a new blocklist configuration.
- **Arguments**:
  - `name` (string, required): Unique identifier for the blocklist
  - `display_name` (string, required): Human-readable name
  - `source_url` (string, required): URL to download the blocklist from
  - `category` (string, optional): Category (default: "malware")
  - `threat_level` (int, optional): Threat level 1-4 (default: 1)
  - `update_frequency` (string, optional): Update frequency (default: "daily")
  - `description` (string, optional): Description of the blocklist
- **Response Example**:
```json
{
  "status": "success",
  "message": "Blocklist abuseipdb added successfully"
}
```
- **Example curl**:
```bash
curl -X POST "http://localhost:8000/api/threat-intel/blocklists" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "abuseipdb",
    "display_name": "AbuseIPDB",
    "source_url": "https://api.abuseipdb.com/api/v2/blacklist",
    "category": "malware",
    "threat_level": 2,
    "description": "AbuseIPDB blacklist"
  }'
```

### `GET /api/threat-intel/blocklists/{name}`
- **Description**: Get specific blocklist configuration and statistics.
- **Path Parameters**:
  - `name` (string, required): Blocklist name
- **Response Example**:
```json
{
  "status": "success",
  "blocklist": {
    "id": 1,
    "name": "firehol_level1",
    "display_name": "FireHOL Level 1",
    "source_url": "https://raw.githubusercontent.com/firehol/blocklist-ipsets/master/firehol_level1.netset",
    "category": "malware",
    "threat_level": 1,
    "update_frequency": "daily",
    "is_active": true,
    "last_updated": "2025-07-24T18:30:00",
    "last_successful_update": "2025-07-24T18:30:00",
    "description": "FireHOL Level 1 blocklist - maximum protection with minimum false positives"
  },
  "stats": {
    "name": "firehol_level1",
    "display_name": "FireHOL Level 1",
    "total_ips": 15000,
    "active_ips": 15000,
    "last_updated": "2025-07-24T18:30:00",
    "last_successful_update": "2025-07-24T18:30:00",
    "is_active": true
  }
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/blocklists/firehol_level1"
```



### `GET /api/threat-intel/blocklists/{name}/status`
- **Description**: Get blocklist update status and statistics.
- **Path Parameters**:
  - `name` (string, required): Blocklist name
- **Response Example**:
```json
{
  "status": "success",
  "blocklist_status": {
    "name": "firehol_level1",
    "display_name": "FireHOL Level 1",
    "total_ips": 15000,
    "active_ips": 15000,
    "last_updated": "2025-07-24T18:30:00",
    "last_successful_update": "2025-07-24T18:30:00",
    "is_active": true
  }
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/blocklists/firehol_level1/status"
```

### `GET /api/threat-intel/check/{ip}`
- **Description**: Check if an IP address is in any blocklist.
- **Path Parameters**:
  - `ip` (string, required): IP address to check
- **Response Example (malicious IP)**:
```json
{
  "status": "success",
  "ip": "192.168.1.100",
  "result": {
    "is_malicious": true,
    "threat_level": 2,
    "blocklists": [
      {
        "name": "firehol_level1",
        "display_name": "FireHOL Level 1",
        "threat_level": 2,
        "reason": "FireHOL Level 1 blocklist",
        "ip_range": "192.168.1.0/24"
      }
    ],
    "total_blocklists": 1
  }
}
```
- **Response Example (clean IP)**:
```json
{
  "status": "success",
  "ip": "8.8.8.8",
  "result": {
    "is_malicious": false,
    "threat_level": 0,
    "blocklists": [],
    "total_blocklists": 0
  }
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/check/8.8.8.8"
```

### `GET /api/threat-intel/stats`
- **Description**: Get overall threat intelligence statistics.
- **Arguments**: None
- **Response Example**:
```json
{
  "status": "success",
  "statistics": {
    "total_blocklists": 2,
    "active_blocklists": 2,
    "total_ips": 25000,
    "active_ips": 25000,
    "blocklists_with_data": 2
  },
  "blocklists": [
    {
      "id": 1,
      "name": "firehol_level1",
      "display_name": "FireHOL Level 1",
      "category": "malware",
      "threat_level": 1,
      "is_active": true,
      "last_updated": "2025-07-24T18:30:00",
      "last_successful_update": "2025-07-24T18:30:00"
    }
  ]
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/stats"
```





### Blocklist Management

The system supports multiple blocklist formats:
- **Netset format**: FireHOL blocklists with CIDR ranges
- **Text format**: Simple IP lists (one IP per line)

Each blocklist can be configured with:
- **Threat levels**: 1-4 (1=lowest, 4=highest)
- **Update frequency**: daily, weekly, monthly
- **Categories**: malware, spam, botnet, etc.
- **Active status**: Enable/disable blocklists

### Enhanced Process Socket Analysis

The threat intelligence system is integrated with process monitoring to provide real-time threat analysis for network connections. When retrieving process details with `threat_check=true`, socket connections are automatically validated against threat intelligence blocklists.

#### Socket Threat Analysis Response Format:
```json
{
  "remote_address": "192.168.1.100:22",
  "socket_status": "ESTABLISHED",
  "threat_analysis": {
    "is_malicious": false,
    "threat_level": 0,
    "blocklist_name": null,
    "total_blocklists": 0,
    "reason": "IP address not found or invalid",
    "recommendation": "No threat detected"
  }
}
```

#### Multiple Blocklist Matches:
When an IP matches multiple blocklists, the system provides comprehensive information:
```json
{
  "remote_address": "0.0.0.1:22",
  "socket_status": "ESTABLISHED",
  "threat_analysis": {
    "is_malicious": true,
    "threat_level": 2,
    "blocklist_name": "FireHOL Level 1",
    "total_blocklists": 3,
    "reason": "IP found in 3 blocklists: FireHOL Level 1 blocklist",
    "recommendation": "High priority: Investigate connection to IP in 3 blocklists (primary: FireHOL Level 1)"
  }
}
```

#### Threat Analysis Fields:
- **is_malicious**: Boolean indicating if the IP is in any blocklist
- **threat_level**: Highest threat level among all matching blocklists (0-4)
- **blocklist_name**: Primary blocklist name (highest threat level)
- **total_blocklists**: Number of blocklists containing this IP
- **reason**: Human-readable explanation of the threat
- **recommendation**: Actionable security recommendation

### Database Schema

The threat intelligence system uses two main tables:

**threat_blocklists**:
- Blocklist configurations and metadata
- Update tracking and status information

**threat_ips**:
- Individual IP addresses from blocklists
- Threat levels and categorization
- Historical tracking (first_seen, last_seen)
- Active status for efficient queries

The complete database schema is available in `mysql_schema.sql` which includes all tables including the threat intelligence tables.

## Host Data Management

### Clear Host Data

**Endpoint**: `DELETE /api/hosts/<hostname>/clear`

**Description**: Completely removes all data for a specific host from the database. This includes all scans, processes, baselines, alerts, and related data. Use with caution as this action is irreversible.

**Parameters**:
- `hostname` (path parameter, string, required): The hostname to clear data for

**Returns**:
- JSON object with status and deletion summary

**Example Response**:
```json
{
  "status": "success",
  "message": "All data for host 'Rev-X-000' has been cleared successfully",
  "deleted": {
    "scans": 16,
    "processes": 2219,
    "host_id": 1
  }
}
```

**Error Responses**:
- `404`: Host not found
- `500`: Database error

**Example curl**:
```bash
curl -X DELETE "http://localhost:8000/api/hosts/Rev-X-000/clear"
```

**Data Removed**:
- All process data (GPU, network, disk, files, sockets, libraries)
- All scan data and related host information
- All baseline data and analytics
- All security alerts and metadata
- The host record itself

**Foreign Key Handling**:
The endpoint properly handles all foreign key constraints by deleting data in the correct order:
1. Process-related data (GPU, network, disk, files, sockets, libraries)
2. Processes
3. Scan-related data (interfaces, disks, USB devices, logged users)
4. Scans
5. Host-related data (metadata, alerts, baselines)
6. Host record

## Enhanced NEW_PROCESS Alert Details

The `/adv-analytics/hosts/<host_id>/alerts` endpoint now provides comprehensive detailed information for `NEW_PROCESS` alert types, including:

### JSON Structure
```json
{
  "alert_type": "NEW_PROCESS",
  "process_name": "example_process",
  "process_exe": "/usr/bin/example",
  "process_args": "example arguments",
  "user_name": "username",  // User running the process
  "process_pid": 12345,
  "process_status": "running",
  "cpu_percent": 2.5,
  "memory_percent": 1.2,
  "open_files": ["/path/to/file1", "/path/to/file2"],
  "loaded_libraries": [
    {"path": "/usr/lib/libexample.so", "hash": "abc123..."}
  ],
  "open_sockets": [
    {"fd": 4, "local_address": "127.0.0.1 8080", "remote_address": "", "status": "LISTEN", "type": 1}
  ],
  "gpu_memory_used": 512,
  "gpu_utilization": 15.5,
  "network_sent_bytes": 1024,
  "network_recv_bytes": 2048,
  "network_connections": 5,
  "disk_read_bytes": 4096,
  "disk_write_bytes": 8192
}
```

### Features
- **Complete Process Information**: Executable path, arguments, PID, status
- **User Context**: Username running the process (essential for security assessment)
- **Resource Usage**: CPU, memory, GPU utilization and memory
- **File System Activity**: Open files and loaded libraries with cryptographic hashes
- **Network Activity**: Open sockets (both listening and connected) with detailed connection information
- **I/O Monitoring**: Network and disk I/O statistics
- **Security Assessment**: All information needed to determine if a process is legitimate or suspicious

This comprehensive data allows security analysts to make informed decisions about process legitimacy and potential threats.

### `GET /adv-analytics/hosts/<host_id>/baseline-quality`
- **Description**: Check if the baseline data quality is sufficient for security analysis. Evaluates scan count, baseline coverage, and age to determine if baseline needs updating.
- **Path Parameters**:
  - `host_id` (int, required): ID of the host to check baseline quality for
- **Response Example**:
```json
{
  "host_id": 2,
  "quality_status": "NEEDS_UPDATE",
  "details": {
    "total_scans": 56,
    "baseline_processes": 0,
    "reasons": [
      "Scan count significantly increased"
    ],
    "message": "Baseline should be updated"
  }
}
```
- **Quality Status Values**:
  - `GOOD`: Baseline quality is sufficient for analysis
  - `NEEDS_UPDATE`: Baseline should be updated (scan count doubled or baseline > 30 days old)
  - `INSUFFICIENT_DATA`: Less than 10 scans available for baseline creation
- **Field Explanations**:
  - `total_scans` (int): Total number of scans available for this host
  - `baseline_processes` (int): Number of processes in baseline data
  - `reasons` (array): List of reasons why baseline needs updating
  - `message` (string): Human-readable explanation of baseline status
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/adv-analytics/hosts/2/baseline-quality"
```

### `GET /adv-analytics/hosts/<host_id>/status`
- **Description**: Get comprehensive security and health status for a host. Analyzes current scan data against baselines to detect anomalies and provide actionable recommendations.
- **Path Parameters**:
  - `host_id` (int, required): ID of the host to analyze
- **Response Example**:
```json
{
  "host_id": 2,
  "security_status": "PROBLEM",
  "health_status": "NORMAL",
  "security_score": 0,
  "health_score": 85,
  "security_alerts": [
    {
      "id": "df19102a-3367-470f-b82c-a34e5668069c",
      "type": "NEW_PROCESS",
      "severity": "WARNING",
      "description": "Unknown process 'kworker/u10:2-events_unbound' detected",
      "recommendation": "Verify if this is authorized software",
      "process_name": "kworker/u10:2-events_unbound",
      "process_hash": "49073664382680352876790478089739942214",
      "user": "root"
    },
    {
      "id": "02cff911-fb7a-490f-ad2c-bc5166a68bce",
      "type": "HIGH_RESOURCE_HOST",
      "severity": "PROBLEM",
      "description": "Critical memory usage: 98.6%",
      "recommendation": "Immediate attention required",
      "resource_value": 98.64513613283634,
      "threshold_value": 90
    }
  ],
  "health_issues": [
    "High disk usage on /dev/sda: 85.2%"
  ],
  "baseline_quality": "NEEDS_UPDATE",
  "last_updated": "2025-01-15T10:30:00Z"
}
```
- **Status Values**:
  - `NORMAL`: No issues detected
  - `WARNING`: Minor issues requiring attention
  - `PROBLEM`: Serious issues requiring immediate action
  - `NO_DATA`: Insufficient data for analysis
- **Alert Types**:
  - `NEW_PROCESS`: Unknown process detected
  - `NEW_USER_PROCESSES`: New user running processes (CRITICAL)
  - `USER_PROCESS_ANOMALY`: User running unusual processes (WARNING)
  - `HIGH_RESOURCE_HOST`: High system resource usage (CPU, Memory, GPU, GPU Memory)
  - `HIGH_RESOURCE_PROCESS`: Process using excessive resources
  - `SUSPICIOUS_NETWORK`: Suspicious network activity
  - `MALICIOUS_IP_CONNECTION`: Connection to malicious IP detected
  - `SUSPICIOUS_FILES`: Suspicious file access patterns
  - `SUSPICIOUS_LIBRARIES`: Suspicious library loading
  - `UNKNOWN_USER`: Unknown user logged in
  - `UNUSUAL_USER_PROCESS`: Process running with unusual user
- **Field Explanations**:
  - `security_score` (int): Security score (0-100, higher is better)
  - `health_score` (int): Health score (0-100, higher is better)
  - `security_alerts` (array): List of security alerts with details
  - `health_issues` (array): List of health issues detected
  - `baseline_quality` (string): Quality status of baseline data
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/adv-analytics/hosts/2/status"
```

### `GET /adv-analytics/hosts/<host_id>/network-security`
- **Description**: Get detailed network security analysis for a host. Validates all network connections against threat intelligence blocklists to detect malicious IP connections and provides comprehensive network security assessment with complete endpoint mapping.
- **Path Parameters**:
  - `host_id` (int, required): ID of the host to analyze network security for
- **Response Example**:
```json
{
  "host_id": 2,
  "scan_id": 70,
  "scan_timestamp": 1753387828,
  "status": "WARNING",
  "security_score": 70,
  "summary": {
    "total_connections": 14,
    "established_connections": 14,
    "listening_connections": 23,
    "malicious_connections": 1,
    "unique_malicious_ips": 1
  },
  "malicious_connections": [
    {
      "process_name": "FAKE-EVIL",
      "process_pid": 9999,
      "process_user": "evil-user",
      "process_exe": "/usr/bin/fake-evil",
      "remote_address": "1.19.1.30 12345",
      "socket_status": "ESTABLISHED",
      "ip_address": "1.19.1.30",
      "threat_level": 1,
      "blocklist_name": "FireHOL Level 1",
      "total_blocklists": 1,
      "reason": "FireHOL Level 1 blocklist"
    }
  ],
  "established_connections": [
    {
      "process_name": "nginx",
      "process_pid": 751,
      "process_user": "www-data",
      "process_exe": "/usr/sbin/nginx",
      "local_address": "54.38.157.70 443",
      "remote_address": "31.61.227.62 3714",
      "connection": "54.38.157.70 443 → 31.61.227.62 3714",
      "ip_address": "31.61.227.62"
    }
  ],
  "listening_connections": [
    {
      "process_name": "nginx",
      "process_pid": 751,
      "process_user": "www-data",
      "process_exe": "/usr/sbin/nginx",
      "local_address": "0.0.0.0 80",
      "listening_on": "0.0.0.0 80"
    },
    {
      "process_name": "mysqld",
      "process_pid": 804,
      "process_user": "mysql",
      "process_exe": "/usr/sbin/mysqld",
      "local_address": "127.0.0.1 3306",
      "listening_on": "127.0.0.1 3306"
    }
  ],
  "last_updated": "2025-07-25T08:43:46.848583"
}
```
- **Status Values**:
  - `SECURE`: No malicious connections detected (score: 100)
  - `WARNING`: 1-2 malicious connections detected (score: 70)
  - `CRITICAL`: 3+ malicious connections detected (score: 30)
  - `NO_DATA`: No scan data available
- **Established Connection Details**:
  - `process_name`: Name of process making the connection
  - `process_pid`: Process ID
  - `process_user`: User running the process
  - `process_exe`: Executable path
  - `local_address`: Source IP and port (IP:PORT)
  - `remote_address`: Destination IP and port (IP:PORT)
  - `connection`: Complete connection mapping (local → remote)
  - `ip_address`: Extracted remote IP for threat analysis
- **Listening Connection Details**:
  - `process_name`: Name of process listening
  - `process_pid`: Process ID
  - `process_user`: User running the process
  - `process_exe`: Executable path
  - `local_address`: IP and port being listened on (IP:PORT)
  - `listening_on`: IP and port being listened on (IP:PORT)
- **Malicious Connection Details**:
  - `process_name`: Name of process making the connection
  - `process_pid`: Process ID
  - `process_user`: User running the process
  - `process_exe`: Executable path
  - `remote_address`: Full socket address (IP:PORT)
  - `socket_status`: Connection status (ESTABLISHED)
  - `ip_address`: Remote IP address
  - `threat_level`: Threat level (1-4, higher is more severe)
  - `blocklist_name`: Name of blocklist containing the IP
  - `total_blocklists`: Number of blocklists containing this IP
  - `reason`: Reason for blocklist inclusion
- **Security Features**:
  - **Real-time threat intelligence validation** against multiple blocklists
  - **Automatic exclusion** of private/localhost IP addresses (127.x.x.x, 10.x.x.x, 192.168.x.x, etc.)
  - **Process-level analysis** showing which processes are connecting to malicious IPs
  - **Threat level assessment** with severity-based recommendations
  - **Multiple blocklist support** for comprehensive threat detection
  - **Complete endpoint mapping** showing local and remote addresses for all connections
  - **Listening port analysis** to identify open services and potential vulnerabilities
- **Field Explanations**:
  - `security_score` (int): Network security score (0-100, higher is better)
  - `malicious_connections` (array): List of connections to malicious IPs
  - `established_connections` (array): List of all active network connections with full endpoint details
  - `listening_connections` (array): List of listening ports with service details
  - `total_connections` (int): Total number of established connections
  - `unique_malicious_ips` (int): Number of unique malicious IP addresses detected
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/adv-analytics/hosts/2/network-security"
```

### `GET /adv-analytics/hosts/<host_id>/alerts`
- **Description**: Get all active security alerts for a host organized by process. Returns both database alerts and real-time heuristic analysis recommendations grouped by process for better readability. **All process-related alerts include comprehensive detailed process information** (executable path, arguments, user, PID, status, resource usage, open files, loaded libraries, open sockets, network I/O, disk I/O).
- **Path Parameters**:
  - `host_id` (int, required): ID of the host to get alerts for
- **Response Example**:
```json
{
  "host_id": 2,
  "processes": [
    {
      "process_info": {
        "process_name": "udisksd",
        "process_hash": "66238355968759264830505174567840674088",
        "user_name": "root",
        "severity": "WARNING"
      },
      "alerts": [
        {
          "alert_source": "database",
          "alert_type": "HEURISTIC_WARNING",
          "severity": "WARNING",
          "description": "Suspicious activity detected: esbuild",
          "recommendation": "Review process 'esbuild' for potential security issues",
          "process_name": "esbuild",
          "process_exe": "/home/dado/CYBERGUARDX/node_modules/tsx/node_modules/@esbuild/linux-x64/bin/esbuild",
          "process_args": "/home/dado/CYBERGUARDX/node_modules/tsx/node_modules/@esbuild/linux-x64/bin/esbuild --service=0.23.1 --ping",
          "user_name": "dado",
          "process_pid": 1252,
          "process_status": "sleeping",
          "process_hash": "",
          "process_arg_hash": "18730113153529836779104262599718836116",
          "cpu_percent": 0.1,
          "memory_percent": 0.310218,
          "open_files": [],
          "loaded_libraries": [
            "{'path': '/home/dado/CYBERGUARDX/node_modules/tsx/node_modules/@esbuild/linux-x64/bin/esbuild', 'hash': '960538638974b315d3ff7b139e5331efb993a2f9eae7b92b5d7a24d29ccf4fc4'}"
          ],
          "open_sockets": [],
          "gpu_memory_used": 0,
          "gpu_utilization": 0,
          "network_sent_bytes": 0,
          "network_recv_bytes": 0,
          "network_connections": 0,
          "disk_read_bytes": 0,
          "disk_write_bytes": 0,
          "created_at": "Wed, 30 Jul 2025 20:43:45 GMT",
          "confirmed_at": null,
          "user_action": null,
          "user_comment": null,
          "heuristic_score": 379,
          "heuristic_suggestions": [
            "Review executable location '/home/dado/CYBERGUARDX/node_modules/tsx/node_modules/@esbuild/linux-x64/bin/esbuild' - verify if legitimate",
            "Review external connections for 'esbuild' - verify if legitimate"
          ]
        }
      ]
    }
  ],
  "total_processes": 121,
  "total_alerts": 245,
  "database_alerts": 229,
  "heuristic_alerts": 16
}
```
- **Field Explanations**:
  - `processes` (array): Array of processes with alerts, sorted by severity (CRITICAL → WARNING → NORMAL)
  - `process_info` (object): Process information block containing:
    - `process_name` (string): Name of the process
    - `process_hash` (string): Hash of the process name
    - `user_name` (string): Username running the process
    - `severity` (string): Overall severity for this process (CRITICAL, WARNING, NORMAL)
  - `alerts` (array): Array of alerts for this process, including both database and heuristic alerts
  - `alert_source` (string): Source of the alert ("database" or "heuristic")
  - `alert_type` (string): Type of security alert (NEW_PROCESS, HIGH_RESOURCE_HOST, MALICIOUS_IP_CONNECTION, HEURISTIC_WARNING, HEURISTIC_CRITICAL, NEW_USER_PROCESSES, USER_PROCESS_ANOMALY, SUSPICIOUS_NETWORK, UNKNOWN_USER)
  - `severity` (string): Alert severity (WARNING, CRITICAL, PROBLEM)
  - `description` (string): Human-readable alert description
  - `recommendation` (string): Suggested action to take
  - `heuristic_score` (int): Security score from heuristic analysis (heuristic alerts only)
  - `heuristic_suggestions` (array): Detailed recommendations from heuristic analysis (heuristic alerts only)
  - `total_processes` (int): Total number of processes with alerts
  - `total_alerts` (int): Total number of alerts (database + heuristic)
  - `database_alerts` (int): Number of alerts from database
  - `heuristic_alerts` (int): Number of alerts from heuristic analysis
  - `created_at` (string): Timestamp when alert was created
  - `confirmed_at` (string): Timestamp when user confirmed/denied (null if pending)
  - `user_action` (string): User response (CONFIRMED, FALSE_POSITIVE, IGNORE, or null)
- **Detailed Process Information (All Process-Related Alerts)**:
  - `process_exe` (string): Full executable path
  - `process_args` (string): Command line arguments
  - `process_pid` (int): Process ID
  - `process_status` (string): Process status (running, sleeping, etc.)
  - `process_hash` (string): Hash of process name
  - `process_arg_hash` (string): Hash of process arguments
  - `cpu_percent` (float): CPU usage percentage
  - `memory_percent` (float): Memory usage percentage
  - `open_files` (array): List of open file paths
  - `loaded_libraries` (array): List of loaded library paths with hashes
  - `open_sockets` (array): List of open network sockets with details
  - `gpu_memory_used` (int): GPU memory usage in bytes
  - `gpu_utilization` (float): GPU utilization percentage
  - `network_sent_bytes` (int): Network bytes sent
  - `network_recv_bytes` (int): Network bytes received
  - `network_connections` (int): Number of network connections
  - `disk_read_bytes` (int): Disk read bytes
  - `disk_write_bytes` (int): Disk write bytes
- **Alert Types**:
  - `NEW_PROCESS`: Unknown process detected that's not in baseline or trusted list
  - `NEW_USER_PROCESSES`: New user running processes (CRITICAL severity)
  - `USER_PROCESS_ANOMALY`: User running unusual processes (WARNING severity)
  - `HIGH_RESOURCE_HOST`: Host-wide high CPU, memory, GPU, or GPU memory usage detected
  - `MALICIOUS_IP_CONNECTION`: Process connected to IP found in threat intelligence blocklists
  - `SUSPICIOUS_NETWORK`: High network activity detected
  - `UNKNOWN_USER`: Multiple users logged in
  - `HEURISTIC_WARNING`: Suspicious activity detected by heuristic analysis
  - `HEURISTIC_CRITICAL`: Critical security risk detected by heuristic analysis

**Note**: All process-related alerts (NEW_PROCESS, HEURISTIC_WARNING, HEURISTIC_CRITICAL, MALICIOUS_IP_CONNECTION, etc.) include comprehensive detailed process information for complete security analysis.
- **Heuristic Analysis**:
  - `severity` (string): Overall security severity (NORMAL, WARNING, CRITICAL, UNKNOWN)
  - `total_score` (int): Combined security score from all heuristic rules
  - `critical_processes` (array): List of process names flagged as critical security risks
  - `warning_processes` (array): List of process names flagged as suspicious
  - `suggestions` (array): Detailed recommendations for each detected issue
- **Heuristic Detection Types**:
  - **Process Name Blacklist**: Detects processes with suspicious names (e.g., "FAKE-EVIL", "backdoor")
  - **Suspicious Paths**: Detects executables in suspicious locations (e.g., `/tmp/`, `/home/`)
  - **Suspicious Arguments**: Detects command line arguments indicating code execution
  - **Suspicious Libraries**: Detects libraries loaded from suspicious locations
  - **Sensitive Files**: Detects access to sensitive system files
  - **Suspicious Ports**: Detects connections to suspicious ports (e.g., 666, 1337)
  - **External Connections**: Detects connections to external IP addresses
  - **Root Suspicious Processes**: Detects root processes running from suspicious locations
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/adv-analytics/hosts/2/alerts"
```

### `POST /adv-analytics/hosts/<host_id>/alerts/<alert_id>/confirm`
- **Description**: Confirm or deny a security alert. This action helps the system learn and improve future detection accuracy. If marked as false positive, the process may be added to trusted list.
- **Path Parameters**:
  - `host_id` (int, required): ID of the host
  - `alert_id` (string, required): UUID of the alert to confirm
- **Request Body** (JSON):
  - `action` (string, required): User action - "CONFIRMED", "FALSE_POSITIVE", or "IGNORE"
  - `comment` (string, optional): Additional comment about the decision
- **Response Example**:
```json
{
  "message": "Alert updated successfully",
  "alert_id": "df19102a-3367-470f-b82c-a34e5668069c",
  "action": "FALSE_POSITIVE",
  "timestamp": "2025-07-25T09:24:48.224776"
}
```
- **Action Values**:
  - `CONFIRMED`: Alert is a real security issue
  - `FALSE_POSITIVE`: Alert is not a security issue (process may be trusted)
  - `IGNORE`: Alert is not a security issue but don't trust the process
- **Learning Behavior**:
  - If marked as `FALSE_POSITIVE`, the process is automatically added to trusted list
  - User confirmations help adjust detection thresholds for future scans
  - System learns from user decisions to improve accuracy
- **Audit Logging**: All alert confirmations are logged with user action, comment, and timestamp
- **Example curl**:
```bash
curl -X POST "http://localhost:8000/adv-analytics/hosts/2/alerts/df19102a-3367-470f-b82c-a34e5668069c/confirm" \
  -H "Content-Type: application/json" \
  -d '{
    "action": "FALSE_POSITIVE",
    "comment": "This is a normal system process"
  }'
```

### `GET /adv-analytics/hosts/<host_id>/trusted-processes`
- **Description**: Get list of trusted processes for a host. Trusted processes are excluded from security alerts as they are known to be safe.
- **Path Parameters**:
  - `host_id` (int, required): ID of the host to get trusted processes for
- **Response Example**:
```json
{
  "host_id": 2,
  "trusted_processes": [
    {
      "id": "abc123-def456-ghi789",
      "host_id": 2,
      "process_name": "sshd",
      "process_hash": "313082042825438427301712014536209208963",
      "added_by": "user_confirmation",
      "added_at": "2025-01-15T10:30:00Z"
    }
  ],
  "total_trusted": 1
}
```
- **Field Explanations**:
  - `id` (string): Unique UUID for the trusted process entry
  - `process_name` (string): Name of the trusted process
  - `process_hash` (string): Hash of the process name
  - `added_by` (string): How the process was added ("user_confirmation", "api", etc.)
  - `added_at` (string): Timestamp when process was added to trusted list
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/adv-analytics/hosts/2/trusted-processes"
```

### `POST /adv-analytics/hosts/<host_id>/trusted-processes`
- **Description**: Add a process to the trusted list for a host. Trusted processes will not generate security alerts in future scans. All additions are logged for audit purposes.
- **Path Parameters**:
  - `host_id` (int, required): ID of the host to add trusted process for
- **Request Body** (JSON):
  - `process_name` (string, required): Name of the process to trust
  - `process_hash` (string, required): Hash of the process name
  - `reason` (string, optional): Reason for trusting this process (default: "No reason provided")
  - `user_id` (string, optional): ID of the user making the request (default: "unknown")
- **Response Example**:
```json
{
  "message": "Process added to trusted list",
  "process_name": "nginx",
  "process_hash": "nginx-hash",
  "user_id": "admin",
  "reason": "Web server",
  "timestamp": "2025-07-25T09:24:29.352818"
}
```
- **Audit Logging**: All trusted process additions are logged with user ID, reason, and timestamp
- **Example curl**:
```bash
curl -X POST "http://localhost:8000/adv-analytics/hosts/2/trusted-processes" \
  -H "Content-Type: application/json" \
  -d '{
    "process_name": "nginx",
    "process_hash": "nginx-hash",
    "reason": "Web server",
    "user_id": "admin"
  }'
```
- **Request Body** (JSON):
  - `process_name` (string, required): Name of the process to trust
  - `process_hash` (string, required): Hash of the process name
- **Response Example**:
```json
{
  "message": "Process added to trusted list",
  "process_name": "sshd",
  "process_hash": "313082042825438427301712014536209208963"
}
```
- **Error Example**:
```json
{
  "error": "Process already trusted"
}
```
- **Example curl**:
```bash
curl -X POST "http://localhost:8000/adv-analytics/hosts/2/trusted-processes" \
  -H "Content-Type: application/json" \
  -d '{
    "process_name": "sshd",
    "process_hash": "313082042825438427301712014536209208963"
  }'
```

### `GET /adv-analytics/hosts/<host_id>/learning-stats`
- **Description**: Get learning statistics for a host. Shows how the system has learned from user confirmations and the effectiveness of the learning algorithm.
- **Path Parameters**:
  - `host_id` (int, required): ID of the host to get learning stats for
- **Response Example**:
```json
{
  "host_id": 2,
  "total_alerts": 35,
  "trusted_processes": 5,
  "confirmation_stats": [
    {
      "user_action": "FALSE_POSITIVE",
      "count": 12
    },
    {
      "user_action": "CONFIRMED",
      "count": 3
    },
    {
      "user_action": "IGNORE",
      "count": 8
    }
  ],
  "learning_effectiveness": "Active"
}
```
- **Field Explanations**:
  - `total_alerts` (int): Total number of alerts generated for this host
  - `trusted_processes` (int): Number of processes in trusted list
  - `confirmation_stats` (array): Breakdown of user confirmations by action type
  - `learning_effectiveness` (string): Status of learning system ("Active", "No data")
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/adv-analytics/hosts/2/learning-stats"
```

## Heuristic Analysis Configuration

The system includes a configurable heuristic analysis engine that uses JSON rule sets to detect suspicious process behavior in real-time.

### Configuration File: `euristic_process_scoring_LINUX.json`

The heuristic analysis is configured through a JSON file that defines security rules for Linux systems:

```json
{
  "metadata": {
    "description": "Linux Process Security Rules - Simple Format",
    "version": "1.0",
    "author": "Rev + ChatGPT"
  },
  "process_names": {
    "whitelist": [
      "systemd", "sshd", "bash", "zsh", "cron", "atd", "dbus-daemon", 
      "udevd", "rsyslogd", "NetworkManager", "nginx", "apache2", 
      "postgres", "mysqld", "docker", "snapd", "cupsd", "Xorg", 
      "gnome-session", "pulseaudio", "VBoxService", "node", "python", 
      "java", "php", "kauditd", "kthreadd", "kworker", "ksoftirqd"
    ],
    "blacklist": [
      "update", "logd", "cleaner", "kernel_service", "watcher", "agent",
      "cr0n", "initd", "svc", "sysup", "host", "setup", "script",
      "backdoor", "payload", "crypto", "miner", "FAKE-EVIL"
    ]
  },
  "paths": {
    "whitelist": [
      "/usr/bin/", "/bin/", "/usr/sbin/", "/sbin/", "/lib/", "/lib64/"
    ],
    "blacklist": [
      "/tmp/", "/dev/shm/", "/var/tmp/", "/run/user/", "/home/", 
      "/root/", "/mnt/", "/media/", "/proc/", "/dev/"
    ]
  },
  "suspicious_arguments": [
    "base64 -d", "bash -c", "curl", "wget", "LD_PRELOAD", 
    "LD_LIBRARY_PATH", "python -c", "perl -e", "ruby -e",
    "bash -i", "/dev/tcp/", "exec", "eval"
  ],
  "suspicious_ports": [
    1337, 2222, 4444, 5555, 9001, 8080, 8888
  ],
  "sensitive_files": [
    "/etc/passwd", "/etc/shadow", "/etc/sudoers", "/root/", 
    "/home/", "/proc/kcore", "/dev/mem", "/dev/sda",
    "/home/*/.ssh/", "/proc/*/mem", "/tmp/.*"
  ],
  "loaded_libraries": {
    "whitelist": [
      "/lib/", "/lib64/"
    ],
    "blacklist": [
      "/tmp/", "/dev/shm/", "/home/", "/mnt/", "/media/"
    ]
  },
  "network_connections": {
    "suspicious_ports": [1337, 2222, 4444, 5555, 9001],
    "external_ip_detection": true,
    "local_subnets": ["10.", "192.168.", "172.16.", "172.17.", "172.18.", "172.19.", "172.20.", "172.21.", "172.22.", "172.23.", "172.24.", "172.25.", "172.26.", "172.27.", "172.28.", "172.29.", "172.30.", "172.31."]
  },
  "user_checks": {
    "root_process_suspicious_paths": true,
    "uid_0_suspicious_locations": true
  },
  "scoring": {
    "process_name_blacklist": 5,
    "path_blacklist": 7,
    "suspicious_arguments": 10,
    "loaded_libraries_blacklist": 6,
    "suspicious_ports": 5,
    "external_ip_connection": 4,
    "sensitive_files": 8,
    "root_process_suspicious": 10,
    "warning_threshold": 10,
    "critical_threshold": 20
  }
}
```

### Rule Categories

#### Process Names
- **Whitelist**: Known safe process names (e.g., systemd, sshd, nginx)
- **Blacklist**: Suspicious process names (e.g., FAKE-EVIL, backdoor, crypto)

#### Paths
- **Whitelist**: Standard system directories (e.g., /usr/bin/, /bin/)
- **Blacklist**: Suspicious locations (e.g., /tmp/, /home/, /dev/)

#### Network Analysis
- **Suspicious Ports**: Ports commonly used by malware (e.g., 666, 1337, 4444)
- **External IP Detection**: Flags connections to external IPs outside local subnets
- **Local Subnets**: Defines trusted local network ranges

#### Scoring System
- **Warning Threshold**: Score at which processes are flagged as suspicious (default: 10)
- **Critical Threshold**: Score at which processes are flagged as critical (default: 20)
- **Individual Rule Scores**: Each rule type has a specific score weight

### Real-time Analysis

The heuristic analysis runs in real-time on the latest scan data and provides:

1. **Process-by-process analysis** using all available data (name, path, arguments, libraries, files, network)
2. **Scoring system** that combines multiple detection methods
3. **Actionable recommendations** for each detected issue
4. **Severity classification** (NORMAL, WARNING, CRITICAL)
5. **Integration with alerts** - heuristic analysis is included in the alerts endpoint

### Example Detection

For a malicious process like FAKE-EVIL:
- **Process Name**: "FAKE-EVIL" matches blacklist (+5 points)
- **Executable Path**: "/tmp/fake-evil" matches suspicious path (+7 points)  
- **Network Connection**: Connection to external IP 1.19.1.30 (+4 points)
- **Listening Port**: Port 666 matches suspicious port (+5 points)
- **Total Score**: 21 points (CRITICAL severity)

## Process Grouping Feature

The `/api/hosts/<host_id>/processes` endpoint now supports process grouping to eliminate duplicates:

### Grouping Parameters
- `group_by_name` (bool, default: `true`): Groups processes by name to return unique processes only

### When `group_by_name=true` (default):
- Returns unique processes instead of all scan instances
- Each process shows aggregated data:
  - `max_cpu_percent`: Highest CPU usage observed
  - `avg_cpu_percent`: Average CPU usage across all scans
  - `max_memory_percent`: Highest memory usage observed  
  - `avg_memory_percent`: Average memory usage across all scans
  - `occurrences`: How many times this process was seen
  - `first_seen`: Timestamp of first occurrence
  - `last_seen`: Timestamp of most recent occurrence

### When `group_by_name=false`:
- Returns all process instances (original behavior)
- Each record represents a specific process at a specific scan time

### Example Usage:
```bash
# Get unique processes (default)
curl "http://localhost:8000/api/hosts/1/processes?group_by_name=true"

# Get all process instances
curl "http://localhost:8000/api/hosts/1/processes?group_by_name=false"
```
- **Error Example**:
```json
{"status": "error", "message": "Host not found"}
```
- **Example curl**:
```bash
curl -X DELETE http://localhost:8000/api/admin/devices/1
```

---

## Threat Intelligence System

The threat intelligence system provides IP blocklist management and malicious IP detection capabilities.

### `GET /api/threat-intel/blocklists`
- **Description**: Get all blocklist configurations.
- **Arguments**: None
- **Response Example**:
```json
{
  "status": "success",
  "blocklists": [
    {
      "id": 1,
      "name": "firehol_level1",
      "display_name": "FireHOL Level 1",
      "source_url": "https://raw.githubusercontent.com/firehol/blocklist-ipsets/master/firehol_level1.netset",
      "category": "malware",
      "threat_level": 1,
      "update_frequency": "daily",
      "is_active": true,
      "last_updated": null,
      "last_successful_update": null,
      "description": "FireHOL Level 1 blocklist - maximum protection with minimum false positives"
    }
  ],
  "total": 1
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/blocklists"
```

### `POST /api/threat-intel/blocklists`
- **Description**: Add a new blocklist configuration.
- **Arguments**:
  - `name` (string, required): Unique identifier for the blocklist
  - `display_name` (string, required): Human-readable name
  - `source_url` (string, required): URL to download the blocklist from
  - `category` (string, optional): Category (default: "malware")
  - `threat_level` (int, optional): Threat level 1-4 (default: 1)
  - `update_frequency` (string, optional): Update frequency (default: "daily")
  - `description` (string, optional): Description of the blocklist
- **Response Example**:
```json
{
  "status": "success",
  "message": "Blocklist abuseipdb added successfully"
}
```
- **Example curl**:
```bash
curl -X POST "http://localhost:8000/api/threat-intel/blocklists" \
  -H "Content-Type: application/json" \
  -d '{
    "name": "abuseipdb",
    "display_name": "AbuseIPDB",
    "source_url": "https://api.abuseipdb.com/api/v2/blacklist",
    "category": "malware",
    "threat_level": 2,
    "description": "AbuseIPDB blacklist"
  }'
```

### `GET /api/threat-intel/blocklists/{name}`
- **Description**: Get specific blocklist configuration and statistics.
- **Path Parameters**:
  - `name` (string, required): Blocklist name
- **Response Example**:
```json
{
  "status": "success",
  "blocklist": {
    "id": 1,
    "name": "firehol_level1",
    "display_name": "FireHOL Level 1",
    "source_url": "https://raw.githubusercontent.com/firehol/blocklist-ipsets/master/firehol_level1.netset",
    "category": "malware",
    "threat_level": 1,
    "update_frequency": "daily",
    "is_active": true,
    "last_updated": "2025-07-24T18:30:00",
    "last_successful_update": "2025-07-24T18:30:00",
    "description": "FireHOL Level 1 blocklist - maximum protection with minimum false positives"
  },
  "stats": {
    "name": "firehol_level1",
    "display_name": "FireHOL Level 1",
    "total_ips": 15000,
    "active_ips": 15000,
    "last_updated": "2025-07-24T18:30:00",
    "last_successful_update": "2025-07-24T18:30:00",
    "is_active": true
  }
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/blocklists/firehol_level1"
```



### `GET /api/threat-intel/blocklists/{name}/status`
- **Description**: Get blocklist update status and statistics.
- **Path Parameters**:
  - `name` (string, required): Blocklist name
- **Response Example**:
```json
{
  "status": "success",
  "blocklist_status": {
    "name": "firehol_level1",
    "display_name": "FireHOL Level 1",
    "total_ips": 15000,
    "active_ips": 15000,
    "last_updated": "2025-07-24T18:30:00",
    "last_successful_update": "2025-07-24T18:30:00",
    "is_active": true
  }
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/blocklists/firehol_level1/status"
```

### `GET /api/threat-intel/check/{ip}`
- **Description**: Check if an IP address is in any blocklist.
- **Path Parameters**:
  - `ip` (string, required): IP address to check
- **Response Example (malicious IP)**:
```json
{
  "status": "success",
  "ip": "192.168.1.100",
  "result": {
    "is_malicious": true,
    "threat_level": 2,
    "blocklists": [
      {
        "name": "firehol_level1",
        "display_name": "FireHOL Level 1",
        "threat_level": 2,
        "reason": "FireHOL Level 1 blocklist",
        "ip_range": "192.168.1.0/24"
      }
    ],
    "total_blocklists": 1
  }
}
```
- **Response Example (clean IP)**:
```json
{
  "status": "success",
  "ip": "8.8.8.8",
  "result": {
    "is_malicious": false,
    "threat_level": 0,
    "blocklists": [],
    "total_blocklists": 0
  }
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/check/8.8.8.8"
```

### `GET /api/threat-intel/stats`
- **Description**: Get overall threat intelligence statistics.
- **Arguments**: None
- **Response Example**:
```json
{
  "status": "success",
  "statistics": {
    "total_blocklists": 2,
    "active_blocklists": 2,
    "total_ips": 25000,
    "active_ips": 25000,
    "blocklists_with_data": 2
  },
  "blocklists": [
    {
      "id": 1,
      "name": "firehol_level1",
      "display_name": "FireHOL Level 1",
      "category": "malware",
      "threat_level": 1,
      "is_active": true,
      "last_updated": "2025-07-24T18:30:00",
      "last_successful_update": "2025-07-24T18:30:00"
    }
  ]
}
```
- **Example curl**:
```bash
curl -X GET "http://localhost:8000/api/threat-intel/stats"
```





### Blocklist Management

The system supports multiple blocklist formats:
- **Netset format**: FireHOL blocklists with CIDR ranges
- **Text format**: Simple IP lists (one IP per line)

Each blocklist can be configured with:
- **Threat levels**: 1-4 (1=lowest, 4=highest)
- **Update frequency**: daily, weekly, monthly
- **Categories**: malware, spam, botnet, etc.
- **Active status**: Enable/disable blocklists

### Enhanced Process Socket Analysis

The threat intelligence system is integrated with process monitoring to provide real-time threat analysis for network connections. When retrieving process details with `threat_check=true`, socket connections are automatically validated against threat intelligence blocklists.

#### Socket Threat Analysis Response Format:
```json
{
  "remote_address": "192.168.1.100:22",
  "socket_status": "ESTABLISHED",
  "threat_analysis": {
    "is_malicious": false,
    "threat_level": 0,
    "blocklist_name": null,
    "total_blocklists": 0,
    "reason": "IP address not found or invalid",
    "recommendation": "No threat detected"
  }
}
```

#### Multiple Blocklist Matches:
When an IP matches multiple blocklists, the system provides comprehensive information:
```json
{
  "remote_address": "0.0.0.1:22",
  "socket_status": "ESTABLISHED",
  "threat_analysis": {
    "is_malicious": true,
    "threat_level": 2,
    "blocklist_name": "FireHOL Level 1",
    "total_blocklists": 3,
    "reason": "IP found in 3 blocklists: FireHOL Level 1 blocklist",
    "recommendation": "High priority: Investigate connection to IP in 3 blocklists (primary: FireHOL Level 1)"
  }
}
```

#### Threat Analysis Fields:
- **is_malicious**: Boolean indicating if the IP is in any blocklist
- **threat_level**: Highest threat level among all matching blocklists (0-4)
- **blocklist_name**: Primary blocklist name (highest threat level)
- **total_blocklists**: Number of blocklists containing this IP
- **reason**: Human-readable explanation of the threat
- **recommendation**: Actionable security recommendation

### Database Schema

The threat intelligence system uses two main tables:

**threat_blocklists**:
- Blocklist configurations and metadata
- Update tracking and status information

**threat_ips**:
- Individual IP addresses from blocklists
- Threat levels and categorization
- Historical tracking (first_seen, last_seen)
- Active status for efficient queries

The complete database schema is available in `mysql_schema.sql` which includes all tables including the threat intelligence tables.

## Host Data Management

### Clear Host Data

**Endpoint**: `DELETE /api/hosts/<hostname>/clear`

**Description**: Completely removes all data for a specific host from the database. This includes all scans, processes, baselines, alerts, and related data. Use with caution as this action is irreversible.

**Parameters**:
- `hostname` (path parameter, string, required): The hostname to clear data for

**Returns**:
- JSON object with status and deletion summary

**Example Response**:
```json
{
  "status": "success",
  "message": "All data for host 'Rev-X-000' has been cleared successfully",
  "deleted": {
    "scans": 16,
    "processes": 2219,
    "host_id": 1
  }
}
```

**Error Responses**:
- `404`: Host not found
- `500`: Database error

**Example curl**:
```bash
curl -X DELETE "http://localhost:8000/api/hosts/Rev-X-000/clear"
```

**Data Removed**:
- All process data (GPU, network, disk, files, sockets, libraries)
- All scan data and related host information
- All baseline data and analytics
- All security alerts and metadata
- The host record itself

**Foreign Key Handling**:
The endpoint properly handles all foreign key constraints by deleting data in the correct order:
1. Process-related data (GPU, network, disk, files, sockets, libraries)
2. Processes
3. Scan-related data (interfaces, disks, USB devices, logged users)
4. Scans
5. Host-related data (metadata, alerts, baselines)
6. Host record

## Enhanced NEW_PROCESS Alert Details

The `/adv-analytics/hosts/<host_id>/alerts` endpoint now provides comprehensive detailed information for `NEW_PROCESS` alert types, including:

### JSON Structure
```json
{
  "alert_type": "NEW_PROCESS",
  "process_name": "example_process",
  "process_exe": "/usr/bin/example",
  "process_args": "example arguments",
  "user_name": "username",  // User running the process
  "process_pid": 12345,
  "process_status": "running",
  "cpu_percent": 2.5,
  "memory_percent": 1.2,
  "open_files": ["/path/to/file1", "/path/to/file2"],
  "loaded_libraries": [
    {"path": "/usr/lib/libexample.so", "hash": "abc123..."}
  ],
  "open_sockets": [
    {"fd": 4, "local_address": "127.0.0.1 8080", "remote_address": "", "status": "LISTEN", "type": 1}
  ],
  "gpu_memory_used": 512,
  "gpu_utilization": 15.5,
  "network_sent_bytes": 1024,
  "network_recv_bytes": 2048,
  "network_connections": 5,
  "disk_read_bytes": 4096,
  "disk_write_bytes": 8192
}
```

### Features
- **Complete Process Information**: Executable path, arguments, PID, status
- **User Context**: Username running the process (essential for security assessment)
- **Resource Usage**: CPU, memory, GPU utilization and memory
- **File System Activity**: Open files and loaded libraries with cryptographic hashes
- **Network Activity**: Open sockets (both listening and connected) with detailed connection information
- **I/O Monitoring**: Network and disk I/O statistics
- **Security Assessment**: All information needed to determine if a process is legitimate or suspicious

This comprehensive data allows security analysts to make informed decisions about process legitimacy and potential threats.
