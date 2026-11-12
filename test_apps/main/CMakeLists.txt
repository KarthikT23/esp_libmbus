# WHOLE_ARCHIVE keeps the TEST_CASE constructors of unreferenced objects
idf_component_register(SRC_DIRS "."
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES esp_libmbus unity
                       WHOLE_ARCHIVE)
