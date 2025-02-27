// Copyright 2023 PingCAP, Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <daemon/GraphiteWriter.h>
#include <daemon/BaseDaemon.h>
#include <Poco/Util/LayeredConfiguration.h>
#include <Poco/Util/Application.h>
#include <Common/getFQDNOrHostName.h>

#include <mutex>
#include <iomanip>


GraphiteWriter::GraphiteWriter(const std::string & config_name, const std::string & sub_path)
{
    Poco::Util::LayeredConfiguration & config = Poco::Util::Application::instance().config();
    port = config.getInt(config_name + ".port", 42000);
    host = config.getString(config_name + ".host", "localhost");
    timeout = config.getDouble(config_name + ".timeout", 0.1);

    root_path = config.getString(config_name + ".root_path", "one_min");

    if (config.getBool(config_name + ".hostname_in_path", true))
    {
        if (!root_path.empty())
            root_path += ".";

        std::string hostname_in_path = getFQDNOrHostName();

        /// Replace dots to underscores so that Graphite does not interpret them as path separators
        std::replace(std::begin(hostname_in_path), std::end(hostname_in_path), '.', '_');

        root_path += hostname_in_path;
    }

    if (sub_path.size())
    {
        if (!root_path.empty())
            root_path += ".";
        root_path += sub_path;
    }
}


std::string GraphiteWriter::getPerServerPath(const std::string & server_name, const std::string & root_path)
{
    std::string path = root_path + "." + server_name;
    std::replace(path.begin() + root_path.size() + 1, path.end(), '.', '_');
    return path;
}
