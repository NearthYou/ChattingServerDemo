#include "Logger.h"
#include <iostream>
#include <chrono>
#include <iomanip>
#include <sstream>

Logger& Logger::GetInstance()
{
    static Logger instance;
    return instance;
}

Logger::Logger() : isInitialized(false)
{
}

Logger::~Logger()
{
    if (logFile.is_open())
    {
        logFile.close();
    }
}

void Logger::SetLogFile(const std::string& filename)
{
    std::lock_guard<std::mutex> lock(logMutex);
    if (logFile.is_open())
    {
        logFile.close();
    }
    logFile.open(filename, std::ios::app);
    isInitialized = logFile.is_open();
}

void Logger::Log(const std::string& message)
{
    if (!isInitialized) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    std::lock_guard<std::mutex> lock(logMutex);
    logFile << "[" << ss.str() << "] " << message << std::endl;
    std::cout << "[" << ss.str() << "] " << message << std::endl;
}

void Logger::LogError(const std::string& message)
{
    if (!isInitialized) return;

    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
    localtime_s(&tm, &time);
    std::stringstream ss;
    ss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");

    std::lock_guard<std::mutex> lock(logMutex);
    logFile << "[" << ss.str() << "] ERROR: " << message << std::endl;
    std::cerr << "[" << ss.str() << "] ERROR: " << message << std::endl;
} 