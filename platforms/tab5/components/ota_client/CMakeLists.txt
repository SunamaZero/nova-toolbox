idf_component_register(SRCS "ota_client.cpp"
                    INCLUDE_DIRS "."
                    REQUIRES esp_https_ota esp_http_client json app_update)
