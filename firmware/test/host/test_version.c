#include "unity.h"
#include "omni/version.h"

void setUp(void) {}
void tearDown(void) {}

static void test_version_string(void)
{
    TEST_ASSERT_EQUAL_STRING("0.1.0", omni_version());
}

int main(void)
{
    UNITY_BEGIN();
    RUN_TEST(test_version_string);
    return UNITY_END();
}
