#include <chrono>
#include <fstream>
#include <filesystem>
#include <fmt/format.h>
#include <fmt/chrono.h>
#include <spdlog/spdlog.h>
#include <random>
#include "ConfigParser.h"
#include "exception/FileNotFound.h"
#include "../exception/NotImplemented.h"

nlohmann::json snspd::io::ConfigParser::get_config(const std::string &path) {

  spdlog::debug("Reading config from {}", path);

  // Make sure that the file exists
  if (!std::filesystem::exists(path)) {
    throw FileNotFound(fmt::format("JSON config file {} does not exist.", path));
  }

  std::ifstream json_file(path);
  nlohmann::json out;
  json_file >> out;
  return out;
}

nlohmann::json snspd::io::ConfigParser::get_config(const std::map<std::string, docopt::value> &args) {

  // Path to the config file
  std::string path = args.at("--config").asString();

  return get_config(path);
}

snspd::Parameters snspd::io::ConfigParser::init_params(const nlohmann::json &config) {

  // Get the initial config values
  auto init_config = config["parameters"];

  // Get the system size
  std::size_t size{init_config["size"].get<std::size_t>()};

  spdlog::debug("Creating parameter struct");
  Parameters params {
      0,
      init_config["maxSteps"].get<unsigned long>(),
      init_config["average"].get<unsigned long>(),
      0,
      size,
      init_config["dt"],
      init_config["q"],
      init_config["c0"],
      init_config["vg"],
      init_config["nl"],
      init_config["i"],
      init_config["ib"],
      init_config["vb"],
      init_config["rt"],
      init_config["rs"],
      init_config["cs"],
      get_param_vector(init_config["ic"], config),
      get_param_vector(init_config["x"], config),
      get_param_vector(init_config["v"], config),
      get_param_vector(init_config["a"], config),
      get_param_vector(init_config["rqp"], config)
  };

  return params;
}

snspd::Settings
snspd::io::ConfigParser::init_settings(const nlohmann::json &config, const std::map<std::string, docopt::value> &args) {
  std::string output;

  // Check if the output is given as a CLI param
  if (args.at("--output")) {
    output = args.at("--output").asString();
  }

  // Check if config sets the output
  else if (config.contains("settings") && config["settings"].contains("output")) {
    output = config["settings"]["output"].get<std::string>();
  }

  else {
    output = "out_{:%Y-%m-%d-%H%M%S}.h5";
  }

  // Get current time
  auto now = std::chrono::system_clock::now();
  time_t tt = std::chrono::system_clock::to_time_t(now);
  tm local_tm = *localtime(&tt);

  Settings settings {
      fmt::format(output, local_tm)
  };

  return settings;
}

std::vector<double> snspd::io::ConfigParser::get_param_vector(const nlohmann::json &vec, const nlohmann::json &config) {

  // Get the system size
  std::size_t size{config["parameters"]["size"].get<std::size_t>()};

  // Create the object to be returned
  std::vector<double> out(size);

  // Fill the vector with a number if a scalar is given
  if (vec.is_number()) {
    std::fill(out.begin(), out.end(), vec.get<double>());
  }

    // Fill the vector with the array values if an array is given
  else if (vec.is_array()) {
    out = config.get<std::vector<double>>();
  }

    // If it has stationaryPhase set to true than fill with vector that makes the phases stationary
  else if (vec.is_object() && vec.contains("stationaryPhase") && vec["stationaryPhase"].get<bool>()) {
    std::size_t multiplier{size};
    double arcsin_ratio{std::asin(std::min(config["parameters"]["ib"].get<double>(), 1.0))};

    std::generate(out.begin(), out.end(), [&]() {
      return static_cast<double>(multiplier--) * arcsin_ratio;
    });
  }

    // If it has random set to true then fill with random values between min and max
  else if (vec.is_object() && vec.contains("random") && vec["random"].get<bool>()) {
    std::random_device uniform_rnd_dev;
    std::mt19937 uniform_rnd_gen(uniform_rnd_dev());
    std::uniform_real_distribution<double> uniform_dist(vec["min"].get<double>(), vec["max"].get<double>());

    std::generate(out.begin(), out.end(), [&](){
      return uniform_dist(uniform_rnd_gen);
    });
  }

    // No method found
  else {
    throw NotImplemented("The selected method for filling the vector is not implemented.");
  }

  return out;
}

void snspd::io::ConfigParser::update_params(std::size_t step) {

  // Update the step
  m_param.step = step;

  // Update the time step
  ++m_param.time_step;

  // Update scalars
  m_param.nl = update_scalar("nl", m_param.nl, step, m_config);
  m_param.i = update_scalar("i", m_param.i, step, m_config);
  m_param.ib = update_scalar("ib", m_param.ib, step, m_config);
  m_param.vb = update_scalar("vb", m_param.vb, step, m_config);
  m_param.rt = update_scalar("rt", m_param.rt, step, m_config);
  m_param.rs = update_scalar("rs", m_param.rs, step, m_config);
  m_param.cs = update_scalar("cs", m_param.cs, step, m_config);
}

double snspd::io::ConfigParser::update_scalar(const std::string &name, double current, std::size_t step,
                                              const nlohmann::json &config) {

  // Do not update the parameters if the update section is missing
  if (!config.contains("updates")) {
    return current;
  }

  auto updates = config["updates"];

  auto result = std::find_if(updates.begin(),
                             updates.end(), [&](const nlohmann::json &update) {
        return update["values"].contains(name) && update["start"].get<std::size_t>() <= step
               && update["end"].get<std::size_t>() >= step;
      });

  if (result != updates.end()) {
    auto start = (*result)["start"].get<std::size_t>();
    auto end = (*result)["end"].get<std::size_t>();
    auto from = (*result)["values"][name]["from"].get<double>();
    auto to = (*result)["values"][name]["to"].get<double>();

    return (to - from) * (static_cast<double>(step - start)) / static_cast<double>(end - start) + from;
  }

  return current;
}
