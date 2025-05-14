#pragma once
#include <string>
#include <fstream>
#include <mutex>

class Logger
{
public:
    static Logger& GetInstance();
    
    void Log(const std::string& message);
    void LogError(const std::string& message);
    void SetLogFile(const std::string& filename);

private:
    Logger();
    ~Logger();

    std::ofstream logFile;
    std::mutex logMutex;
    bool isInitialized;
}; 