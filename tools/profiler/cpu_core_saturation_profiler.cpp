#include <windows.h>
#include <iostream>
#include <vector>
#include <chrono>

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: test_proc_cpu <cmd_line>\n";
        return 1;
    }

    std::string full_cmd = GetCommandLineA();
    size_t pos = full_cmd.find(".exe");
    if (pos == std::string::npos) return 1;
    pos = full_cmd.find(' ', pos);
    if (pos == std::string::npos) return 1;
    while (pos < full_cmd.size() && full_cmd[pos] == ' ') pos++;
    std::string cmd = full_cmd.substr(pos);

    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcessA(NULL, &cmd[0], NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::cerr << "Failed to start process\n";
        return 1;
    }

    auto t0 = std::chrono::high_resolution_clock::now();
    WaitForSingleObject(pi.hProcess, INFINITE);
    auto t1 = std::chrono::high_resolution_clock::now();

    FILETIME createTime, exitTime, kernelTime, userTime;
    GetProcessTimes(pi.hProcess, &createTime, &exitTime, &kernelTime, &userTime);

    ULARGE_INTEGER kTime, uTime;
    kTime.LowPart = kernelTime.dwLowDateTime;
    kTime.HighPart = kernelTime.dwHighDateTime;
    uTime.LowPart = userTime.dwLowDateTime;
    uTime.HighPart = userTime.dwHighDateTime;

    double wall_sec = std::chrono::duration<double>(t1 - t0).count();
    double cpu_sec = (kTime.QuadPart + uTime.QuadPart) / 10000000.0;
    double kernel_sec = kTime.QuadPart / 10000000.0;
    double user_sec = uTime.QuadPart / 10000000.0;

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    int num_cores = sysInfo.dwNumberOfProcessors; // 24

    double overall_cpu_utilization = (cpu_sec / (wall_sec * num_cores)) * 100.0;

    std::cout << "\n================ CPU PROFILING REPORT ================\n";
    std::cout << "Wall Clock Time : " << wall_sec << " s\n";
    std::cout << "Total CPU Time  : " << cpu_sec << " s (User: " << user_sec << " s, Kernel: " << kernel_sec << " s)\n";
    std::cout << "CPU Core Count  : " << num_cores << "\n";
    std::cout << "Equivalent Cores: " << (cpu_sec / wall_sec) << " fully saturated cores\n";
    std::cout << "Avg Utilization : " << overall_cpu_utilization << " % across all " << num_cores << " cores\n";
    std::cout << "======================================================\n";

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}
