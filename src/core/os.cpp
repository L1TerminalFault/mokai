#include "os.hpp"
#include <cstdlib>

#if defined(__linux__)
#define MOKAI_PLATFORM_LINUX
#include <sys/wait.h>
#include <unistd.h>
#elif defined(__APPLE__) && defined(__MACH__)
#define MOKAI_PLATFORM_MACOS
#include <sys/wait.h>
#include <unistd.h>
#elif defined(_WIN32) || defined(_WIN64)
#define MOKAI_PLATFORM_WINDOWS
#include <windows.h>
#endif

namespace mokai {

Platform OS::GetCurrentPlatform() {
#if defined(MOKAI_PLATFORM_LINUX)
  return Platform::Linux;
#elif defined(MOKAI_PLATFORM_MACOS)
  return Platform::MacOS;
#elif defined(MOKAI_PLATFORM_WINDOWS)
  return Platform::Windows;
#else
  return Platform::Unknown;
#endif
}

std::string OS::GetPlatformName() {
  switch (GetCurrentPlatform()) {
  case Platform::Linux:
    return "Linux";
  case Platform::MacOS:
    return "macOS";
  case Platform::Windows:
    return "Windows";
  default:
    return "Unknown";
  }
}

std::filesystem::path OS::GetTemporaryDirectory() {
  return std::filesystem::temp_directory_path();
}

std::string OS::GetSharedLibraryExtension() {
#if defined(MOKAI_PLATFORM_WINDOWS)
  return ".dll";
#elif defined(MOKAI_PLATFORM_MACOS)
  return ".dylib";
#else
  return ".so";
#endif
}

std::string OS::GetStaticLibraryExtension() {
#if defined(MOKAI_PLATFORM_WINDOWS)
  return ".lib";
#else
  return ".a";
#endif
}

std::string OS::GetExecutableExtension() {
#if defined(MOKAI_PLATFORM_WINDOWS)
  return ".exe";
#else
  return "";
#endif
}

std::filesystem::path OS::FindExecutable(const std::string &name) {
  std::string command;
#if defined(MOKAI_PLATFORM_WINDOWS)
  command = "where " + name + " > nul 2>&1";
#else
  command = "which " + name + " > /dev/null 2>&1";
#endif

  if (std::system(command.c_str()) == 0) {
    return name;
  }
  return "";
}

int OS::ExecuteCommand(
    const std::string &command,
    const std::unordered_map<std::string, std::string> &env) {
  std::string full_expression;

#if defined(MOKAI_PLATFORM_WINDOWS)
  if (!env.empty()) {
    for (const auto &[key, value] : env) {
      full_expression += "set " + key + "=" + value + " && ";
    }
  }
  full_expression += command;
#else
  if (!env.empty()) {
    for (const auto &[key, value] : env) {
      full_expression += key + "=" + value + " ";
    }
  }
  full_expression += command;
#endif

  return std::system(full_expression.c_str());
}

int OS::ExecuteBinaryAndForwardStreams(const std::string &binary_path) {
#if defined(MOKAI_PLATFORM_WINDOWS)
  STARTUPINFOA si;
  PROCESS_INFORMATION pi;
  ZeroMemory(&si, sizeof(si));
  si.cb = sizeof(si);
  // Bind I/O handles directly to the parent process streams
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
  ZeroMemory(&pi, sizeof(pi));

  // Create child process execution string
  std::string cmd = binary_path;

  if (!CreateProcessA(NULL, &cmd[0], NULL, NULL, TRUE, 0, NULL, NULL, &si,
                      &pi)) {
    return -1;
  }

  // Await binary process termination loop
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);
  return static_cast<int>(exit_code);

#else
  // POSIX (Linux / macOS) implementation bypassing subshell wrapping overhead
  pid_t pid = fork();

  if (pid == -1) {
    return -1; // Fork failure
  }

  if (pid == 0) {
    // Inside Child Process execution context
    char *argv[] = {const_cast<char *>(binary_path.c_str()), nullptr};
    char *envp[] = {nullptr}; // Inherits environment state cleanly

    execv(binary_path.c_str(), argv);

    // If execv returns, an execution failure occurred
    std::exit(127);
  } else {
    // Inside Parent Process execution context: Await tracking ID termination
    int status = 0;
    if (waitpid(pid, &status, 0) == -1) {
      return -1;
    }

    if (WIFEXITED(status)) {
      return WEXITSTATUS(status); // Normal execution return loop code
    } else if (WIFSIGNALED(status)) {
      return 128 +
             WTERMSIG(status); // Process crashed or received terminating signal
    }
    return -1;
  }
#endif
}

} // namespace mokai
