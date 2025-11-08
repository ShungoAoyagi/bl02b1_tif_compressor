#include "common.hpp"

std::mutex cout_mutex;

std::string zeroPad(int number, int width)
{
    std::ostringstream oss;
    oss << std::setw(width) << std::setfill('0') << number;
    return oss.str();
}

