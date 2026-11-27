#include <bq_log/misc/bq_log_c_api.h>

#include <stdio.h>
#include <string.h>

/* log_arg_type_enum::string_utf8_type (include/bq_log/misc/bq_log_def.h). */
#define FORMAT_UTF8 17

static int fetched;
static bq_log_level fetched_level;
static char fetched_text[256];

static void BQ_STDCALL on_fetch(void* param, uint64_t log_id,
                                int32_t category_idx, bq_log_level level,
                                const char* content, int32_t length) {
  size_t n = (size_t)length < sizeof fetched_text - 1
                 ? (size_t)length
                 : sizeof fetched_text - 1;
  (void)param;
  (void)log_id;
  (void)category_idx;
  ++fetched;
  fetched_level = level;
  memcpy(fetched_text, content, n);
  fetched_text[n] = '\0';
}

static int fail(const char* what) {
  fprintf(stderr, "FAIL: %s (%d fetched, last: %s)\n", what, fetched,
          fetched_text);
  return 1;
}

int main(void) {
  static const char message[] = "written from C";
  const size_t tail = sizeof message - 1;
  bq_api_log_write_handle handle;
  bq_api_string_def name;
  const uint32_t* levels;
  uint64_t id;
  size_t len;

  if (strcmp(__api_get_log_version(), "2.8.3") != 0) {
    return fail("__api_get_log_version");
  }

  __api_set_console_buffer_enable(true);
  id = __api_create_log("cabin_c",
                        "appenders_config.con.type=console\n"
                        "appenders_config.con.levels=[warning]\n",
                        0, NULL);
  if (id == 0) {
    return fail("__api_create_log");
  }
  if (!__api_get_log_name_by_id(id, &name) || name.len != 7 ||
      memcmp(name.str, "cabin_c", 7) != 0) {
    return fail("__api_get_log_name_by_id");
  }

  levels = __api_get_log_merged_log_level_bitmap_by_log_id(id);
  if (levels == NULL || (*levels & (1u << bq_log_level_info)) != 0 ||
      (*levels & (1u << bq_log_level_warning)) == 0) {
    return fail("__api_get_log_merged_log_level_bitmap_by_log_id");
  }

  handle = __api_log_write_begin(id, bq_log_level_warning, 0, FORMAT_UTF8,
                                 (uint32_t)tail, message, 0);
  if (handle.result != bq_buffer_result_code_success) {
    return fail("__api_log_write_begin");
  }
  __api_log_write_finish(id, handle);
  __api_force_flush(id);
  while (__api_fetch_and_remove_console_buffer(on_fetch, NULL)) {
  }

  len = strlen(fetched_text);
  if (fetched != 1 || fetched_level != bq_log_level_warning || len < tail ||
      strcmp(fetched_text + len - tail, message) != 0) {
    return fail("console entry");
  }
  return 0;
}
