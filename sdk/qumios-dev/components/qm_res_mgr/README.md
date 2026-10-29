# qm_res_mgr

中文版本请见 [README_CN.md](README_CN.md)。

`qm_res_mgr` is a resource manager built on `qm_http_client`. It fetches resource
list/detail metadata and downloads a resource URL synchronously through a data
callback.

The component only owns resource download. Product or business code owns file
hit checks, file persistence, cache validation, and storage cleanup.

## Architecture

```text
application
    | request metadata / consume download bytes
    v
qm_res_mgr
    |-- HTTP or MQTT metadata channel
    `-- qm_http_client download channel
    v
resource service / signed resource URL
```

The component returns metadata and streams bytes through caller callbacks. It
does not create files, retain downloaded payloads, or decide cache policy.

## Configuration Ownership

Use this rule of thumb: values that vary by request, device, account, or selected
resource are API inputs; fixed component behavior and resource limits are
`qm_config`/Kconfig.

### API inputs

These values are intentionally passed through the public APIs because callers may
need to change them at runtime:

- `qm_res_mgr_set_env()`: current API environment for HTTP metadata builds.
  The compiled default comes from Kconfig, but products and examples can switch
  environment at runtime when HTTP metadata is compiled in. MQTT-only builds
  return `-QM_ENOSYS`.
- `qm_res_mgr_get_list()`: device token, DID, resource bundle name, start page
  number, page size, and an optional per-page callback.
- `qm_res_mgr_get_category()`: product id, DID, and the HTTP auth or MQTT
  handle required by the compiled metadata channel for first-level categories
  and their second-level children.
- `qm_res_mgr_get_children_category()`: product id, DID, optional
  `categoryId`, optional `resourceBundleName`, and the HTTP auth or MQTT
  handle required by the compiled metadata channel.
- `qm_res_mgr_get_detail()`: DID, resource id, and the HTTP auth or MQTT
  handle required by the compiled metadata channel.
- `qm_res_mgr_download()`: selected resource metadata, data callback, and
  callback user pointer.

Because the metadata channel is a compile-time one-of-two choice, request
struct fields in `qm_res_mgr.h` are also compiled per channel with
`CONFIG_QM_RES_MGR_HTTP_SUPPORT` / `CONFIG_QM_RES_MGR_MQTT_SUPPORT`:

- HTTP builds expose HTTP-specific fields such as `token` and category
  `product_id`
- MQTT builds expose MQTT-specific fields such as `iot_handle` and
  `timeout_ms`

Keep these out of `qm_config`: token, DID, bundle name, resource id, resource
URL, file metadata returned by the server, callback user pointer, and storage
policy. The token is only an input for HTTP metadata API requests; file
download uses the resource URL directly.

`qm_res_mgr` no longer has a runtime config struct or event callback layer.
Errors and status are returned directly by `qm_res_mgr_get_list()`,
`qm_res_mgr_get_detail()`, and `qm_res_mgr_download()`.

HTTPS requests use `qm_http_client` transport defaults: one-way TLS with no
resource-manager-specified CA certificate or client certificate.

### `qm_config`/Kconfig

Keep fixed component switches, limits, and tuning in `qm_config`/Kconfig:

- component enable switch: `CONFIG_QM_RES_MGR_SUPPORT`
- exactly one metadata channel: `CONFIG_QM_RES_MGR_HTTP_SUPPORT` or
  `CONFIG_QM_RES_MGR_MQTT_SUPPORT`
- generated `qm_config.h` defines only the selected metadata-channel symbol;
  the other channel macro stays undefined
- common download config that stays visible after the component is enabled:
  `CONFIG_QM_RES_MGR_SEGMENT_SIZE`, `CONFIG_QM_RES_MGR_RETRY_MAX`
- metadata shared config shown after a metadata channel is selected:
  `CONFIG_QM_RES_MGR_PAGE_SIZE`
- HTTP metadata-only config, shown only when
  `CONFIG_QM_RES_MGR_HTTP_SUPPORT=y`: `CONFIG_QM_RES_MGR_TIMEOUT_MS`,
  `CONFIG_QM_RES_MGR_HTTP_RX_BUF_SIZE`,
  `CONFIG_QM_RES_MGR_HTTP_HEADER_MAX`, `CONFIG_QM_RES_MGR_API_BODY_MAX`,
  `CONFIG_QM_RES_MGR_ENV_DEV`, `CONFIG_QM_RES_MGR_ENV_TEST`, or
  `CONFIG_QM_RES_MGR_ENV_PROD`
- MQTT metadata-only config, shown only when
  `CONFIG_QM_RES_MGR_MQTT_SUPPORT=y`:
  `CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT`

Logging is controlled independently from `qm_http_client` and the MQTT/cloud
components. `CONFIG_QM_RES_MGR_LOG_CONFIG` is the master switch; only when it is
enabled are the category options shown: `CONFIG_QM_RES_MGR_LOG_ERROR`,
`CONFIG_QM_RES_MGR_LOG_LIFECYCLE`, `CONFIG_QM_RES_MGR_LOG_CONTROL`,
`CONFIG_QM_RES_MGR_LOG_METADATA`, `CONFIG_QM_RES_MGR_LOG_DOWNLOAD`,
`CONFIG_QM_RES_MGR_LOG_PAYLOAD`, and `CONFIG_QM_RES_MGR_LOG_STATS`.

With the master switch disabled, only error and lifecycle logs are enabled.
The mask can also be changed at runtime with
`qm_res_mgr_set_log_flags(QM_RES_MGR_LOG_ERROR | QM_RES_MGR_LOG_DOWNLOAD)` and
read with `qm_res_mgr_get_log_flags()`. Error logs report status and lengths
instead of dumping response bodies. Device identifiers, bundle names, ETags,
signatures, and timestamp fields are logged as lengths or presence flags;
download URLs are shortened before logging.

Keep these out of API request structs: timeouts, segment size, retry count, and
HTTP buffer/header/body limits.

When MQTT metadata is compiled in, resource file download still uses HTTP
internally, but the HTTP timeout/header/read-buffer knobs are not exposed in
`qm_kconfig`; that path falls back to the component defaults of 15000 ms,
2048-byte header max, and 1024-byte read buffer.

`CONFIG_QM_RES_MGR_API_BODY_MAX` is an upper bound for list/detail API response
bodies. It is not preallocated; the response buffer grows with received data and
fails if the body exceeds this configured limit.

Resource API base URLs are fixed inside the component:

- `QM_RES_MGR_ENV_DEV`: `https://api-dev.ljyd.cn`
- `QM_RES_MGR_ENV_TEST`: `https://api-test.ljyd.cn`
- `QM_RES_MGR_ENV_PROD`: `https://api.ljyd.cn`

The default environment is production when Kconfig does not override it. When
HTTP metadata is compiled in, it can be changed at runtime with:

```c
qm_res_mgr_set_env(QM_RES_MGR_ENV_TEST);
```

The API does not accept arbitrary `api_base_url` input. The component is ready
to use without `qm_res_mgr_init()` or `qm_res_mgr_deinit()`.

This one-of-two choice only applies to metadata APIs:
`qm_res_mgr_get_list()`, `qm_res_mgr_get_category()`, and
`qm_res_mgr_get_children_category()`. Resource file download still uses the
HTTP URL returned by the service.

## Download Boundary

`qm_res_mgr_download()` runs in the caller's thread. It starts a network download
from the selected resource URL and calls `on_data` for every received chunk. It
does not check whether a file already exists locally, does not read from local
files, does not write downloaded bytes to storage, and does not own cache
metadata.

Business code should handle storage around the stream:

- before `qm_res_mgr_download()`: decide whether a local file can be used directly
- inside `on_data`: write bytes to the target file or decoder
- after completion or error: commit, remove, or retry partial files according to
  product policy

Retry inside `qm_res_mgr` only covers network segment retries. It uses the
current delivered byte count as the next `Range` start. If `on_data` returns a
non-zero value, the download stops and that value is returned to the caller.

When the component knows the expected total size, a download only succeeds when
`downloaded_size == total_size`. Early EOF, short responses, or any final size
mismatch now return an error instead of reporting a partial file as success.

If the first transfer does not support `Range` and also does not provide
`Content-Length`, the component treats the bytes read in that transfer as the
final size. Business code should still validate that result against file format
or checksum expectations when needed.

## List Pagination

`qm_res_mgr_get_list()` returns server pagination metadata in
`qm_res_mgr_list_t`. The request uses `pageNum=1` when `page_num` is 0 and uses
`CONFIG_QM_RES_MGR_PAGE_SIZE` when `page_size` is 0. When the response `pages`
value is greater than the current page, `qm_res_mgr_get_list()` continues
requesting the remaining pages and writes parsed resources directly into the
final caller-owned `items` array.

If `req->on_page` is set, `qm_res_mgr_get_list()` also calls it synchronously
after each page is parsed successfully and before the next page is requested.
That callback receives a borrowed `qm_res_mgr_list_page_t` view for the current
page, so product code can render or forward partial results immediately without
waiting for all pages. `page.items` points into the final `qm_res_mgr_list_t`
array owned by the caller, so the page slice remains valid until the full list
is released with `qm_res_mgr_list_free()`.

When MQTT metadata is compiled in, the component assigns an internal request
`id` and matches replies by reply method plus that `id`. Multiple
`qm_res_mgr_get_list()`, `qm_res_mgr_get_category()`, and
`qm_res_mgr_get_children_category()` MQTT requests may be in flight at the same
time; each caller should still set an appropriate timeout for its own request.

After a list item is selected, call `qm_res_mgr_download()` with that item's
`url` to download the file resource directly. Do not call
`qm_res_mgr_get_detail()` only to obtain a download URL when the list item
already contains one.

`qm_res_mgr_get_detail()` now follows the compiled metadata channel too:
HTTP builds use `/resource/detail`, while MQTT builds use
`getResourceDetail/getResourceDetailReply`. If the list item already contains
the final URL, it is still better to download directly instead of calling
detail only to resolve the same URL again.

Resource metadata string fields are dynamically allocated from the server values.
Release list results with `qm_res_mgr_list_free()`. Release a single resource
returned by `qm_res_mgr_get_detail()` with `qm_res_mgr_resource_free()`.
`qm_res_mgr_download_result_t` contains only plain status and size fields and
does not require a release call.

The authorization token is only passed to HTTP-mode `qm_res_mgr_get_list()` and
`qm_res_mgr_get_detail()`. File download uses the selected item's `url`
directly and does not add the business `Authorization` header. If a file
service needs authorization, business code should provide a downloadable URL
that already encodes the required access policy, such as a signed URL.

`qm_res_mgr_set_env()` changes one component-global environment only for HTTP
metadata builds. The component now keeps each list/detail call on the
environment snapshot captured at that call's entry, but concurrent product
tasks that keep switching environments can still affect later requests.
Product code should avoid mixing multiple environments concurrently inside one
runtime instance. MQTT-only metadata builds do not use this setting and return
`-QM_ENOSYS`.

- `total`: total resource count
- `page_num`: first requested page number
- `page_size`: current page size from `pageSize`
- `pages`: total page count
- `count`: number of parsed items across fetched pages
- `status_code`: last list API HTTP status, preserved even when
  `qm_res_mgr_get_list()` returns an error

## Category APIs

`qm_res_mgr_get_category()` uses the compiled metadata channel:

- HTTP: calls `/agent-device/resource/getCategory?pid=...&did=...`
- MQTT: sends `geCategory` and parses `geCategoryReply`

`qm_res_mgr_get_children_category()` also uses the compiled metadata channel:

- HTTP: calls `/agent-device/resource/getChildrenCategory` with `pid`, `did`,
  and optional `categoryId` / `resourceBundleName`
- MQTT: sends `geChildrenCategory` and parses `geChildrenCategoryReply`

The child-category request can filter by either `categoryId`,
`resourceBundleName`, or both; leaving both empty means "children visible to
this device" according to the cloud service.

`qm_res_mgr_get_category()` exports caller-owned `qm_res_mgr_category_list_t`
results. Release them with `qm_res_mgr_category_list_free()`. First-level
category nodes use `children` only for second-level categories.

`qm_res_mgr_get_children_category()` exports caller-owned
`qm_res_mgr_child_category_list_t` results. Release them with
`qm_res_mgr_child_category_list_free()`. Returned child nodes do not include a
`children` field.

Parsed first-level category fields include:

- `id`
- `name`
- `resource_bundle_name`
- `parent_id`
- `level`
- `is_leaf`
- `sort_order`
- `create_time`
- `description`
- `icon`
- `background_image`
- `child_count`
- `children`

`qm_res_mgr_get_children_category()` returns the same shared fields except for
`child_count` and `children`. If the service includes deeper nested
`children`, the current component ignores them.

When HTTP metadata is compiled in, `req->product_id` and `req->did` are
mandatory; `req->token` is optional and passed through as the HTTP
authorization header when present. When MQTT metadata is compiled in,
`req->iot_handle` is mandatory and `req->timeout_ms == 0` falls back to the
Kconfig default reply timeout
`CONFIG_QM_RES_MGR_MQTT_TIMEOUT_MS_DEFAULT`.

## Resource Metadata Fields

Besides the original list/detail fields, `qm_res_mgr_resource_t` now also parses
these optional resource properties when the service provides them:

- `audio_type`: value from `audioType`
- `total_duration`: value from `totalDuration`, in seconds

## opencpu Example

The opencpu example is in:

```text
apps/opencpu/app/qm_res_mgr_example.c
```

Select it by setting:

```c
#define APP_DEMO_SELECT APP_DEMO_RES_MGR
```

Useful example macros:

```c
#define QM_RES_MGR_EXAMPLE_TOKEN        "device-token"
#define QM_RES_MGR_EXAMPLE_DID          "device-did"
#define QM_RES_MGR_EXAMPLE_BUNDLE       "resource-bundle-name"
#define QM_RES_MGR_EXAMPLE_RESOURCE_ID  0
#define QM_RES_MGR_EXAMPLE_ENV          QM_RES_MGR_ENV_DEV
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_CATEGORY          1
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_CHILDREN_CATEGORY 1
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_LIST              1
#define QM_RES_MGR_EXAMPLE_ENABLE_GET_DETAIL            1
#define QM_RES_MGR_EXAMPLE_ENABLE_DOWNLOAD              1
```

These macros let the example enable or disable each interface block separately:

- `QM_RES_MGR_EXAMPLE_ENABLE_GET_CATEGORY`: test `qm_res_mgr_get_category()`
- `QM_RES_MGR_EXAMPLE_ENABLE_GET_CHILDREN_CATEGORY`: test `qm_res_mgr_get_children_category()`
- `QM_RES_MGR_EXAMPLE_ENABLE_GET_LIST`: test `qm_res_mgr_get_list()`
- `QM_RES_MGR_EXAMPLE_ENABLE_GET_DETAIL`: test `qm_res_mgr_get_detail()` directly, and allow detail fallback when a list item has no `url`
- `QM_RES_MGR_EXAMPLE_ENABLE_DOWNLOAD`: test `qm_res_mgr_download()`, including the direct URL branch

The example only counts downloaded bytes in `on_data`. Add file save or file-hit
logic in product code if the demo needs to persist resources.

For direct download testing without list/detail APIs:

```c
#define QM_RES_MGR_EXAMPLE_DIRECT_URL "https://example.com/resource.bin"
```
