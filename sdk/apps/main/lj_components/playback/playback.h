#ifndef LJYD_PLAYBACK_H
#define LJYD_PLAYBACK_H

#include "qm_config.h"
#include "qm_audio.h"

#if defined(CONFIG_QM_RES_MGR_SUPPORT) && CONFIG_QM_RES_MGR_SUPPORT
#include "qm_res_mgr.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Metadata credentials used by the compiled resource-manager channel.
 *
 * The pointers are borrowed for the duration of each metadata call. Callers
 * must invoke list/detail APIs from an application worker, not an MQTT receive
 * callback, because the resource manager performs synchronous requests.
 */
typedef struct {
    const char *did;
#if defined(CONFIG_QM_RES_MGR_HTTP_SUPPORT) && CONFIG_QM_RES_MGR_HTTP_SUPPORT
    uint32_t product_id;
    const char *token;
#elif defined(CONFIG_QM_RES_MGR_MQTT_SUPPORT) && CONFIG_QM_RES_MGR_MQTT_SUPPORT
    void *iot_handle;
#endif
} playback_metadata_t;

/** @brief Create a Control on the already-started qm_audio. */
int playback_init(void);

/** @brief Submit a PCM URI for playback. */
int playback_play_url(const char *url,
                      uint32_t file_size,
                      uint32_t duration_s);

/** @brief Submit a local PCM file for playback. */
int playback_play_file(const char *path,
                       uint32_t file_size,
                       uint32_t duration_s);

/**
 * @brief One cloud player/play request copied into the playback worker.
 *
 * The pointers are borrowed only for the duration of the call; the component
 * copies all strings before returning.
 */
typedef struct {
    const char *guide_words;
    const char *url;
    const char *audio_type;
    const char *title;
    uint32_t file_size;
    uint32_t duration_s;
} playback_cloud_play_request_t;

/** @brief Queue a cloud resource request without blocking the MQTT callback. */
int playback_submit_cloud_play(const playback_cloud_play_request_t *request);

#if defined(CONFIG_QM_RES_MGR_SUPPORT) && CONFIG_QM_RES_MGR_SUPPORT
/** @brief Select the resource service environment for HTTP metadata builds. */
int playback_set_resource_env(qm_res_mgr_env_t env);

/** @brief Fetch and log the first resource page, then release its storage. */
int playback_print_first_page(const playback_metadata_t *metadata,
                              const char *bundle);

/** @brief Fetch first-level categories; release the result with QUMI's free API. */
int playback_get_categories(const playback_metadata_t *metadata,
                            qm_res_mgr_category_list_t *out);

/** @brief Fetch child categories; release the result with QUMI's free API. */
int playback_get_child_categories(const playback_metadata_t *metadata,
                                  uint32_t category_id,
                                  const char *bundle,
                                  qm_res_mgr_child_category_list_t *out);

/** @brief Fetch one resource detail and submit its URL to the player. */
int playback_play_id(const playback_metadata_t *metadata, uint32_t resource_id);

/** @brief Download resource bytes through an application-owned callback. */
int playback_download(const qm_res_mgr_resource_t *resource,
                      qm_res_mgr_download_data_cb_t on_data,
                      void *user,
                      qm_res_mgr_download_result_t *result);
#endif

/** @brief Pause the most recently accepted playback operation. */
int playback_pause(void);

/** @brief Resume the most recently accepted playback operation. */
int playback_resume(void);

/** @brief Stop the most recently accepted playback operation. */
int playback_stop(void);

/** @brief Read the current operation snapshot. */
int playback_get_status(qm_audio_control_snapshot_t *status);

/** @brief Destroy the Control. qm_audio itself stays up. */
int playback_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* LJYD_PLAYBACK_H */
