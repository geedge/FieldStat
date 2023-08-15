
#include <gtest/gtest.h>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <algorithm>
#include "logger/logger.h"


// void logger_init();

// void logger_destroy();

// void logger_print_info(const char *fmt, ...);
// void logger_print_warn(const char *fmt, ...);
// void logger_print_error(const char *fmt, ...);


namespace fs = std::filesystem;
using namespace std;
int count_log_files(const fs::path& path) {
    return std::count_if(fs::directory_iterator(path), {}, 
        [](const fs::directory_entry& entry) {
            return entry.path().string().find(".log") != std::string::npos;
        }
    );
}

void clear_log_files(const fs::path& path) {
    for (const auto & entry : fs::directory_iterator(path))
    {
        if (entry.path().string().find(".log") != std::string::npos)
        {
            fs::remove(entry.path());
        }
    }
}


string find_log_file() {
    for (const auto & entry : fs::directory_iterator(fs::current_path()))
    {

        if (entry.path().string().find(".log") != std::string::npos)
        {         // cppcheck-suppress [useStlAlgorithm]
            return entry.path().string();
        }
    }
    return "";
}

TEST(unit_test_logger, write_error)
{
    auto path = fs::current_path();
    clear_log_files(path);
    logger_init();
    logger_print_error("debug error! %d", 1);
    EXPECT_EQ(count_log_files(path), 1);

    string line;
    ifstream log_file(find_log_file());
    if (log_file.is_open())
    {
        getline(log_file, line);
        EXPECT_NE(line.find("debug error!"), string::npos);
        log_file.close();
    }
    else
    {
        FAIL();
    }

    logger_destroy();
}

TEST(unit_test_logger, write_many_times)
{
    // clear log files
    auto path = fs::current_path();
    clear_log_files(path);
    
    logger_init();
    for (int i = 0; i < 10000; i++)
    {
        logger_print_error("debug info %d", i);
    }

    EXPECT_EQ(count_log_files(path), 1);

    logger_destroy();
}

int main(int argc, char *argv[]) 
{
	testing::InitGoogleTest(&argc, argv);
	int ret = RUN_ALL_TESTS();
    clear_log_files(fs::current_path());
    return ret;
}