#include <string>
#include <map>
#include <filesystem>
#include <regex>
#include <fstream>
#include <stdexcept>
#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <signal.h>
#include <stdbool.h>
#include <fmt/core.h>
#include <fmt/color.h>
#include <winpp/console.hpp>
#include <winpp/parser.hpp>
#include <winpp/win.hpp>
#include <winpp/system-mutex.hpp>
#include <nlohmann/json.hpp>
using json = nlohmann::ordered_json;

/*============================================
| Declaration
==============================================*/
// program version
const std::string PROGRAM_NAME = "AutoLaunch";
const std::string PROGRAM_VERSION = "1.4.4";

// default length in characters to align status 
constexpr std::size_t g_status_len = 80;

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
  fmt::print(fmt::fg(color) | fmt::emphasis::bold, "[{}]\n", text);
};

// execute a sequence of actions with tags
void exec(const std::string& str, std::function<void()> fct)
{
  fmt::print(fmt::emphasis::bold, "{:<" + std::to_string(g_status_len) + "}", str + ": ");
  try
  {
    fct();
    add_tag(fmt::color::green, "OK");
  }
  catch (const std::exception& ex)
  {
    add_tag(fmt::color::red, "KO");
    throw ex;
  }
}

// replace a substring inside a string
const std::string replace_string(const std::string& str,
                                 const std::string& old_value,
                                 const std::string& new_value)
{
  const std::size_t pos = str.find(old_value);
  if (pos != std::string::npos)
  {
    std::string new_str = str.substr(0, pos);
    new_str += new_value;
    new_str += str.substr(pos + old_value.size());
    return new_str;
  }
  return str;
}

// replace a variable in string by values stored in variables
bool replace_vars(std::string& str,
                  const std::map<std::string, std::string>& variables,
                  const std::string& old_value,
                  const std::string& key,
                  const char old_char = 0,
                  const char new_char = 0)
{
  const auto& it = variables.find(key);
  if (it == variables.end())
    return false;
  std::string new_value = it->second;
  if (old_char != 0 && new_char != 0)
    std::replace(new_value.begin(), new_value.end(), old_char, new_char);
  str = replace_string(str, old_value, new_value);
  return true;
}

// update string with existing variables
const std::string update_var(const std::string& str,
                             const std::map<std::string, std::string>& variables)
{
  // replace sub-string with pattern ${xxx, 'x', 'y'} by their corresponding variable value
  std::string new_str = str;
  {
    std::regex replace_pattern(R"(\$\{([^,\}]+),[^']'(.)',[^']'(.)'\})");
    do
    {
      auto rit = std::sregex_iterator(new_str.begin(), new_str.end(), replace_pattern);
      if (rit == std::sregex_iterator())
        break;
      const std::string old_value = rit->str(0);
      const std::string key = rit->str(1);
      const char old_char = rit->str(2).at(0);
      const char new_char = rit->str(3).at(0);
      if (!replace_vars(new_str, variables, old_value, key, old_char, new_char))
        throw std::runtime_error(fmt::format("missing variable: ${{{}}}", key));
    } while (true);
  }

  // replace sub-string with pattern ${xxx} by their corresponding variable value
  {
    std::regex replace_pattern(R"(\$\{(.*?)\})");
    do
    {
      auto rit = std::sregex_iterator(new_str.begin(), new_str.end(), replace_pattern);
      if (rit == std::sregex_iterator())
        break;
      const std::string old_value = rit->str(0);
      const std::string key = rit->str(1);
      if (!replace_vars(new_str, variables, old_value, key))
        throw std::runtime_error(fmt::format("missing variable: ${{{}}}", key));
    } while (true);
  }

  // there shouldn't be any sub-string with pattern ${xxx}
  if (std::regex_search(new_str, std::regex(R"(\$\{.*?\})")))
    throw std::runtime_error(fmt::format("this pattern can't be replaced: \"{}\"", new_str));

  // replace sub-string with pattern $[xxx] or $<xxx> by the absolute path with quotes
  //  surrounded by double-quotes for $[xxx] => "xxx"
  //  surrounded by single-quotes for $<xxx> => 'xxx'
  {
    std::regex replace_pattern(R"(\$([\[<])(.*?)([\]>]))");
    do
    {
      auto rit = std::sregex_iterator(new_str.begin(), new_str.end(), replace_pattern);
      if (rit == std::sregex_iterator())
        break;
      const std::string old_value = rit->str(0);
      std::string key = rit->str(2);
      key.erase(std::remove(key.begin(), key.end(), '\"'), key.end());
      key.erase(std::remove(key.begin(), key.end(), '\''), key.end());
      char quote;
      if (rit->str(1).at(0) == '[' && rit->str(3).at(0) == ']')
        quote = '\"';
      else if (rit->str(1).at(0) == '<' && rit->str(3).at(0) == '>')
        quote = '\'';
      else
        throw std::runtime_error(fmt::format("wrong pattern detected: \"{}\"", old_value));
      new_str = replace_string(new_str, old_value, quote + std::filesystem::absolute(std::filesystem::path(key)).string() + quote);
      new_str = utf8::from_utf8(new_str);
    } while (true);
  }

  return new_str;
};

// parse command-line option as key:value
std::map<std::string, std::string> parse_cmd(const std::vector<std::string>& vars)
{
  auto split = [](const std::string& str) -> std::pair<std::string, std::string> {
    std::size_t pos = str.find(':');
    if (pos == std::string::npos)
      throw std::runtime_error(fmt::format("invalid command-line option: \"{}\"", str));
    return std::pair<std::string, std::string>{str.substr(0, pos), str.substr(pos+1)};
  };
  std::map<std::string, std::string> variables;
  for (const auto& v : vars)
  {
    auto [key, value] = split(v);
    variables[key] = update_var(value, variables);
  }
  return variables;
}

// display the list of variables
void display_variables(const std::map<std::string, std::string>& variables)
{
  for (const auto& v : variables)
    fmt::print("  {:<30}: {}\n", v.first, v.second);
  fmt::print("\n");
}

// parse tasks json file
std::pair<json, std::map<std::string, std::string>> parse_json(const std::filesystem::path& path,
                                                               const std::map<std::string, std::string>& variables)
{
  // parse json file
  std::ifstream file(path);
  if (!file.good())
    throw std::runtime_error(fmt::format("can't open file: \"{}\"", path.filename().u8string()));
  const json& db = json::parse(file);

  // check json format
  if((!db.contains("description")  || !db["description"].is_string()) ||
     (!db.contains("variables")    || !db["variables"].is_array())    ||
     (!db.contains("tasks-groups") || !db["tasks-groups"].is_array()))
    throw std::runtime_error(fmt::format("invalid tasks file format: \"{}\"", path.filename().u8string()));
  for (const auto& tasks_groups : db["tasks-groups"])
  {
    if((!tasks_groups.contains("tasks") || !tasks_groups["tasks"].is_array()))
      throw std::runtime_error(fmt::format("invalid tasks file format: \"{}\" (incorrect tasks group format)", path.filename().u8string()));
    for (const auto& task : tasks_groups["tasks"])
    {
      if ((!task.contains("description") || !task["description"].is_string()) ||
          (!task.contains("cmd")         || !task["cmd"].is_string())         ||
          (!task.contains("args")        || !task["args"].is_string()))
        throw std::runtime_error(fmt::format("invalid tasks file format: \"{}\" (incorrect task format)", path.filename().u8string()));
    }
  }

  // update variables
  std::map<std::string, std::string> all_vars = variables;
  std::map<std::string, std::string> json_vars;
  for (const auto& v : db["variables"])
  {
    for (const auto& [key, value] : v.items())
    {
      const std::string& new_value = update_var(value.get<std::string>(), all_vars);
      all_vars[key] = new_value;
      json_vars[key] = new_value;
    }
  }

  return std::pair<json, std::map<std::string, std::string>>(db, json_vars);
}

// execute one task - blocking
void execute_task(const std::string& cmd,
                  const std::string& args,
                  std::string& logs,
                  const bool display,
                  const bool ignore_error,
                  const std::chrono::milliseconds& timeout)
{
  // define callback for logs
  logs.clear();
  auto cb_logs = [&logs, display](const std::string& l) -> void {
    if (display)
      fmt::print("{}", l);
    logs += l;
  };

  // define callback for the program exit
  std::mutex mtx;
  std::condition_variable cv;
  int exit_code = 0;
  volatile bool stopped = false;
  auto cb_exit = [&](const int ret) -> void {
    exit_code = ret;
    stopped = true;
    cv.notify_all();
  };

  // start process in async mode (to display logs continuously)
  win::async_process process;
  process.set_default_error_code(-1);
  process.set_working_dir(std::filesystem::current_path());
  if (timeout.count())
    process.set_timeout(timeout);
  if (!process.execute(fmt::format("{} {}", cmd, args), cb_logs, cb_exit))
    throw std::runtime_error("can't start process");

  // wait for process to terminate
  std::unique_lock<std::mutex> lock(mtx);
  cv.wait(lock, [&]{ return stopped; });
  if (!ignore_error && exit_code != 0)
    throw std::runtime_error(fmt::format("process failed with error: {}", exit_code));
}

// execute all the json tasks
void execute_tasks(const json& tasks_groups,
                   std::map<std::string, std::string>& vars,
                   bool interactive)
{
  // lambda helpers
  auto get_bool_value = [=](const json& obj, const std::string& key) -> bool {
    if (obj.contains("flags") && obj["flags"].is_object())
      if (obj["flags"].contains(key) && obj["flags"][key].is_boolean())
        return obj["flags"][key].get<bool>();    
    return (vars.find(key) != vars.end()) ? (vars.at(key) == "true") : false; 
  };
  auto get_float_value = [=](const json& obj, const std::string& key) -> float { 
    if (obj.contains("flags") && obj["flags"].is_object())
      if (obj["flags"].contains(key) && obj["flags"][key].is_number_float())
        return obj["flags"][key].get<float>();
    return (vars.find(key) != vars.end()) ? std::stof(vars.at(key)) : 0.0f; 
  };
  auto to_ms = [](const float timeout) -> std::chrono::milliseconds { 
    return std::chrono::milliseconds(static_cast<std::size_t>(1000.0 * timeout)); 
  };

  // declare system wide mutex to avoid executing tasks in parallel - not locked by default
  win::system_mutex mtx("Global\\AutoLaunchSystemMtx");
  std::unique_lock<win::system_mutex> lck(mtx, std::defer_lock);
  auto lock = [&]() -> void { if (!lck.owns_lock()) lck.lock(); };
  auto unlock = [&]() -> void { if (lck.owns_lock()) lck.unlock(); };

  // lock the whole process if "protected" command-line option is set
  const bool cmd_protected = ((vars.find("protected") != vars.end()) &&
                              (vars["protected"] == "true"));
  if (cmd_protected)
    lock();

  // execute all grouped tasks
  for (const auto& tasks_group : tasks_groups)
  {
    // lock the group of tasks if "protected" group flag is set
    const bool group_protected = (tasks_group.contains("flags") && tasks_group["flags"].is_object()) &&
                                 (tasks_group["flags"].contains("protected") && tasks_group["flags"]["protected"].is_boolean()) &&
                                 (tasks_group["flags"]["protected"].get<bool>());
    if (!cmd_protected && group_protected)
      lock();

    // execute all tasks of this group
    for (const auto& task : tasks_group["tasks"])
    {
      // lock the task if "protected" task flag is set
      const bool task_protected = (task.contains("protected") && task["protected"].is_boolean()) &&
                                  (task["protected"].get<bool>());
      if (!cmd_protected && !group_protected && task_protected)
        lock();

      // read task execution flags - by order: task/group/command-line - default: false or 0.0f
      const bool display_flag =                       task.contains("display") ?      task["display"]     .get<bool>()  : get_bool_value(tasks_group, "display");
      const bool debug_flag =                         task.contains("debug") ?        task["debug"]       .get<bool>()  : get_bool_value(tasks_group, "debug");
      const bool ignore_error_flag =                  task.contains("ignore-error") ? task["ignore-error"].get<bool>()  : get_bool_value(tasks_group, "ignore-error");
      const bool ask_execute_flag =                   task.contains("ask-execute") ?  task["ask-execute"] .get<bool>()  : get_bool_value(tasks_group, "ask-execute");
      const bool ask_continue_flag =                  task.contains("ask-continue") ? task["ask-continue"].get<bool>()  : get_bool_value(tasks_group, "ask-continue");
      const std::chrono::milliseconds timeout = to_ms(task.contains("timeout") ?      task["timeout"]     .get<float>() : get_float_value(tasks_group, "timeout"));

      // read task parameters
      const std::string& desc = fmt::format("\"{}\"", update_var(task["description"].get<std::string>(), vars));
      const std::string& cmd = update_var(task["cmd"].get<std::string>(), vars);
      const std::string& args = update_var(task["args"].get<std::string>(), vars);

      if (debug_flag)
      {
        // display generated task command-line
        fmt::print("{} {}\n",
          fmt::format(fmt::emphasis::bold, "debugging task:"),
          desc);
        fmt::print("{} [{}]\n",
          fmt::format(fmt::emphasis::bold, "task-cmd:"),
          fmt::format("{} {}", cmd, utf8::to_utf8(args)));
      }
      else
      {
        // ask user if it's ok to execute this task
        if (interactive && ask_execute_flag)
        {
          if (!console::ask_user(fmt::format("Do you want to execute the task: {}?", desc)))
          {
            if (!cmd_protected && !group_protected && task_protected)
              unlock();
            continue;
          }
        }

        std::string logs;
        try
        {
          // execute task
          if (display_flag)
            fmt::print("{} {}\n", fmt::format(fmt::emphasis::bold, "execute:"), desc);
          else
            fmt::print("{} {:<80}", fmt::format(fmt::emphasis::bold, "execute"), desc + ":");
          execute_task(cmd, args, logs, display_flag, ignore_error_flag, timeout);

          // parse logs to add new variables
          if (task.contains("parse-variables"))
          {
            // remove all new-lines
            logs.erase(std::remove(logs.begin(), logs.end(), '\r'), logs.end());
            logs.erase(std::remove(logs.begin(), logs.end(), '\n'), logs.end());

            // extract all variable using regex to find their values in the logs
            for (const auto& var : task["parse-variables"])
            {
              for (const auto& [key, value] : var.items())
              {
                std::smatch sm;
                std::regex reg(value.get<std::string>());
                if (std::regex_search(logs, sm, reg))
                  vars[key] = sm.str(1);
              }
            }
          }

          // update variables
          if (task.contains("variables"))
          {
            for (const auto& var : task["variables"])
            {
              for (const auto& [key, value] : var.items())
                vars[key] = update_var(value.get<std::string>(), vars);
            }
          }

          if (!display_flag)
            add_tag(fmt::color::green, "OK");
        }
        catch (const std::exception& ex)
        {
          std::exception err = ex;
          if (!display_flag)
          {
            add_tag(fmt::color::red, "KO");
            err = std::runtime_error(fmt::format("{}\n\n{}", ex.what(), logs));
          }
          else
            fmt::print("\n");
          throw err;
        }
      }

      // unlock the task if necessary
      if (!cmd_protected && !group_protected && task_protected)
        unlock();

      // ask user if it's ok to continue
      if (interactive && ask_continue_flag)
      {
        if (!console::ask_user("Do you want to continue?"))
          throw std::runtime_error("stop requested");
      }
    }

    // unlock the group of tasks if necessary
    if (!cmd_protected && group_protected)
      unlock();
  }

  // unlock the whole process if necessary
  if (cmd_protected)
    unlock();
}

int main(int argc, char** argv)
{
  // initialize Windows console
  console::init(1280, 600);

  // register signal handler
  signal(SIGINT, exit_program);

  // parse command-line arguments
  std::filesystem::path tasks_file;
  std::vector<std::string> variables_str;
  bool interactive = false;
  console::parser parser(PROGRAM_NAME, PROGRAM_VERSION);
  parser.add("t", "tasks", "set the path to json tasks file", tasks_file, true)
        .add("x", "variables", "define a list of variables for the tasks", variables_str)
        .add("i", "interactive", "enable the interactive mode which asks user for questions", interactive);
  if (!parser.parse(argc, argv))
  {
    parser.print_usage();
    return -1;
  }

  int ret;
  try
  {
    // check arguments validity
    if (!std::filesystem::exists(tasks_file) ||
        tasks_file.extension().string() != ".json")
      throw std::runtime_error(fmt::format("the tasks file is invalid: \"{}\"", tasks_file.filename().u8string()));

    // parse command-line options
    std::map<std::string, std::string> cmd_vars;
    exec("parsing command-line variables", [&]() { cmd_vars = parse_cmd(variables_str); });
    display_variables(cmd_vars);

    // parsing tasks json file
    json tasks_db;
    std::map<std::string, std::string> json_vars;
    exec("parsing json-file variables and check validity", [&]() {
      const auto& [db, vars] = parse_json(tasks_file, cmd_vars);
      tasks_db = db;
      json_vars = vars;
      });
    display_variables(json_vars);

    // fusion the two variable lists
    std::map<std::string, std::string> vars;
    vars.insert(cmd_vars.begin(), cmd_vars.end());
    vars.insert(json_vars.begin(), json_vars.end());

    // execute tasks
    fmt::print(fmt::format("{} \"{}\"\n", 
      fmt::format(fmt::emphasis::bold, "Starting:"), 
      update_var(tasks_db["description"].get<std::string>(), vars)));
    execute_tasks(tasks_db["tasks-groups"], vars, interactive);
    ret = 0;
  }
  catch (const std::exception& ex)
  {
    fmt::print("{} {}\n",
      fmt::format(fmt::fg(fmt::color::red) | fmt::emphasis::bold, "error:"),
      ex.what());
    ret = -1;
  }

  // prompt user to terminate the program
  if (interactive)
    system("pause");

  return ret;
}