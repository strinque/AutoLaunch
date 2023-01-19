#include <string>
#include <filesystem>
#include <regex>
#include <signal.h>
#include <stdbool.h>
#include <fmt/core.h>
#include <fmt/color.h>
#include <winpp/console.hpp>
#include <winpp/parser.hpp>
#include <nlohmann/json.hpp>

using json = nlohmann::ordered_json;

/*============================================
| Declaration
==============================================*/
// program version
const std::string PROGRAM_NAME = "AutoLaunch";
const std::string PROGRAM_VERSION = "1.0";

/*============================================
| Function definitions
==============================================*/
// define the function to be called when ctrl-c is sent to process
void exit_program(int signum)
{
  fmt::print("event: ctrl-c called => stopping program\n");
}

// lambda function to show colored tags
auto add_tag = [](const fmt::color color, const std::string& text) {
  fmt::print(fmt::format(fmt::fg(color) | fmt::emphasis::bold, "[{}]\n", text));
};

int main(int argc, char** argv)
{
  // initialize Windows console
  console::init();

  // register signal handler
  signal(SIGINT, exit_program);

  // parse command-line arguments
  std::filesystem::path tasks_file;
  console::parser parser(PROGRAM_NAME, PROGRAM_VERSION);
  parser.add("t", "tasks", "set the path to json tasks file", tasks_file, true);
  if (!parser.parse(argc, argv))
  {
    parser.print_usage();
    return -1;
  }
  if (!std::filesystem::exists(tasks_file) ||
      tasks_file.extension().string() != ".json")
  {
    fmt::print("{} {}\n",
      fmt::format(fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error: "),
      fmt::format("the tasks file is invalid: \"{}\"", tasks_file.filename().string()));
    return -1;
  }

  try
  {
    // execute tasks
    fmt::print(fmt::format(fmt::emphasis::bold, "{:<60}", "executing tasks:"));
    add_tag(fmt::color::green, "OK");
    return 0;
  }
  catch (const std::exception& ex)
  {
    add_tag(fmt::color::red, "KO");
    fmt::print("{} {}\n",
      fmt::format(fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error: "),
      ex.what());
    return -1;
  }
}