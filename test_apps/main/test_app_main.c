#include <stdio.h>
#include "sdkconfig.h"
#include "unity.h"
#include "unity_test_runner.h"

void app_main(void)
{
#if CONFIG_TB_RUN_MENU
    unity_run_menu();
#else
    UNITY_BEGIN();
    unity_run_all_tests();
    UNITY_END();
    printf("test run finished\n");
#endif
}
