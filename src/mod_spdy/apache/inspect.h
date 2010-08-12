// Copyright 2010 Google Inc.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#ifndef MOD_SPDY_APACHE_INSPECT_H_
#define MOD_SPDY_APACHE_INSPECT_H_

#include <map>
#include <string>
#include <vector>

#include "base/basictypes.h"
#include "httpd.h"
#include "util_filter.h"

namespace mod_spdy {

// Helper class for the Inspect() function; not to be used directly.
class Inspector {
 public:
  Inspector();
  ~Inspector();

  // If the given pointer is non-null and is not already in the output table,
  // add it to the output table using the appropriate Populate() method.
  template <typename T>
  void SubInspect(T* object) {
    const void* key = static_cast<void*>(object);
    if (key != NULL && output_.count(key) == 0) {
      Populate(object, &(output_[key]));
    }
  }

  // Given the original pointer we were interested in, format and log the
  // contents of the output table.
  void Print(void* main_key);

 private:
  typedef std::vector<std::string> InspectEntry;
  typedef std::map<const void*, InspectEntry> InspectMap;

  // Populate an output entry vector with data about an object of a particular
  // type.
  void Populate(ap_filter_t* obj, InspectEntry* entry);
  void Populate(apr_table_t* obj, InspectEntry* entry);
  void Populate(conn_rec* object, InspectEntry* entry);
  void Populate(request_rec* object, InspectEntry* entry);
  void Populate(server_rec* obj, InspectEntry* entry);

  InspectMap output_;

  DISALLOW_COPY_AND_ASSIGN(Inspector);
};

// Inspect an Apache object and log information about it and about any objects
// that it references.
template <typename T>
void Inspect(T* object) {
  Inspector inspector;
  inspector.SubInspect(object);
  inspector.Print(object);
}

}  // namespace mod_spdy

#endif  // MOD_SPDY_APACHE_INSPECT_H_
