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

#include "mod_spdy/apache/inspect.h"

#include "base/logging.h"
#include "base/string_util.h"
#include "third_party/apache/httpd/src/include/http_log.h"

#include "mod_spdy/apache/pool_util.h"

namespace mod_spdy {

Inspector::Inspector() {}

Inspector::~Inspector() {}

void Inspector::Print(void* main_key) {
  LocalPool local;
  CHECK(local.status() == APR_SUCCESS);
  const std::string header = StringPrintf("Inspecting %p:", main_key);
  ap_log_perror(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, local.pool(),
                "%s", header.c_str());
  for (InspectMap::const_iterator iter1 = output_.begin(),
           end1 = output_.end(); iter1 != end1; ++iter1) {
    const void* key = iter1->first;
    const std::string subheader = StringPrintf("%p:", key);
    ap_log_perror(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, local.pool(),
                  (key == main_key ? "> %s" : "  %s"), subheader.c_str());
    const InspectEntry& entry = iter1->second;
    for (InspectEntry::const_iterator iter2 = entry.begin(),
             end2 = entry.end(); iter2 != end2; ++iter2) {
      ap_log_perror(APLOG_MARK, APLOG_NOTICE, APR_SUCCESS, local.pool(),
                    "    %s", iter2->c_str());
    }
  }
}

void Inspector::Populate(ap_filter_t* obj, InspectEntry* entry) {
  entry->push_back("[filter chain]");
  for (ap_filter_t* filter = obj; filter; filter = filter->next) {
    entry->push_back(StringPrintf("%s {%d, r=%p, c=%p}", filter->frec->name,
                                  filter->frec->ftype, filter->r, filter->c));
    SubInspect(filter->r);
    SubInspect(filter->c);
  }
}

namespace {

int AddAprTableRow(void* ptr, const char* key, const char* value) {
  std::vector<std::string>* entry =
      static_cast<std::vector<std::string>*>(ptr);
  entry->push_back(StringPrintf("%s: %s", key, value));
  return 1;
}

}  // namespace

void Inspector::Populate(apr_table_t* obj, InspectEntry* entry) {
  entry->push_back("[apr_table_t]");
  apr_table_do(AddAprTableRow, entry, obj, NULL);
}

void Inspector::Populate(conn_rec* obj, InspectEntry* entry) {
  entry->push_back("[conn_rec]");
  entry->push_back(StringPrintf("pool = %p", obj->pool));
  entry->push_back(StringPrintf("base_server = %p *", obj->base_server));
  SubInspect(obj->base_server);
  entry->push_back(StringPrintf("remote_ip = %s", obj->remote_ip));
  entry->push_back(StringPrintf("remote_host = %s", obj->remote_host));
  entry->push_back(StringPrintf("remote_logname = %s", obj->remote_logname));
  entry->push_back(StringPrintf("keepalives = %d", obj->keepalives));
  entry->push_back(StringPrintf("local_ip = %s", obj->local_ip));
  entry->push_back(StringPrintf("local_host = %s", obj->local_host));
  entry->push_back(StringPrintf("id = %ld", obj->id));
  entry->push_back(StringPrintf("input_filters = %p *", obj->input_filters));
  SubInspect(obj->input_filters);
  entry->push_back(StringPrintf("output_filters = %p *", obj->output_filters));
  SubInspect(obj->output_filters);
  entry->push_back(StringPrintf("data_in_input_filters = %d",
                                obj->data_in_input_filters));
  entry->push_back(StringPrintf("clogging_input_filters = %d",
                                obj->clogging_input_filters));
}

void Inspector::Populate(request_rec* obj, InspectEntry* entry) {
  entry->push_back("[request_rec]");
  entry->push_back(StringPrintf("pool = %p", obj->pool));
  entry->push_back(StringPrintf("connection = %p *", obj->connection));
  SubInspect(obj->connection);
  entry->push_back(StringPrintf("server = %p *", obj->server));
  SubInspect(obj->server);
  entry->push_back(StringPrintf("next = %p *", obj->next));
  SubInspect(obj->next);
  entry->push_back(StringPrintf("prev = %p *", obj->prev));
  SubInspect(obj->prev);
  entry->push_back(StringPrintf("main = %p *", obj->main));
  SubInspect(obj->main);
  entry->push_back(StringPrintf("the_request = %s", obj->the_request));
  entry->push_back(StringPrintf("assbackwards = %d", obj->assbackwards));
  entry->push_back(StringPrintf("protocol = %s", obj->protocol));
  entry->push_back(StringPrintf("status_line = %s", obj->status_line));
  entry->push_back(StringPrintf("status = %d", obj->status));
  entry->push_back(StringPrintf("method = %s", obj->method));
  entry->push_back(StringPrintf("method_number = %d", obj->method_number));
  entry->push_back(StringPrintf("sent_bodyct = %d",
                                static_cast<int>(obj->sent_bodyct)));
  entry->push_back(StringPrintf("bytes_sent = %d",
                                static_cast<int>(obj->bytes_sent)));
  entry->push_back(StringPrintf("chunked = %d", obj->chunked));
  entry->push_back(StringPrintf("headers_in = %p *", obj->headers_in));
  SubInspect(obj->headers_in);
  entry->push_back(StringPrintf("headers_out = %p *", obj->headers_out));
  SubInspect(obj->headers_out);
  entry->push_back(StringPrintf("err_headers_out = %p *",
                                obj->err_headers_out));
  SubInspect(obj->err_headers_out);
  entry->push_back(StringPrintf("eos_sent = %d", obj->eos_sent));
}

void Inspector::Populate(server_rec* obj, InspectEntry* entry) {
  entry->push_back("[server_rec]");
  entry->push_back(StringPrintf("process = %p", obj->process));
  entry->push_back(StringPrintf("next = %p *", obj->next));
  SubInspect(obj->next);
}

}  // namespace mod_spdy
