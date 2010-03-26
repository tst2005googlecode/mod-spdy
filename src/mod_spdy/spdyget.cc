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

// This is a quick-hack utility to send SPDY requests to localhost and read
// back replies, useful for debugging mod_spdy.  Its use of sockets is not very
// robust; maybe it can be improved in the future.  It is not, of course,
// suitable for any kind of production use.

#include <sys/socket.h>

#include <cstdio>
#include <set>
#include <string>

#include "base/basictypes.h"
#include "base/scoped_ptr.h"
#include "net/spdy/spdy_framer.h"

namespace {

// Read a line of text from stdin.
std::string ReadLine() {
  std::string str;
  while (true) {
    const unsigned char c = getchar();
    if (c < ' ' || '\x7f' <= c) {
      return str;
    }
    str.push_back(c);
  }
}

// Dump a blob of data to stdout in a pretty-printed hex format.
void HexDump(const std::string& data) {
  const int stride = 16;
  for (int start = 0; start < data.size(); start += stride) {
    printf(" ");
    for (int i = start; i < start + stride; ++i) {
      if (i < data.size()) {
        const unsigned char ch = data[i];
        printf(" %c%c",
               "0123456789abcdef"[(ch & 0xF0) >> 4],
               "0123456789abcdef"[ch & 0x0F]);
      } else {
        printf("   ");
      }
      if (i % stride == stride / 2 - 1) {
        printf(" ");
      }
    }
    printf("  | ");
    for (int i = start; i < start + stride; ++i) {
      if (i < data.size()) {
        const unsigned char ch = data[i];
        if (' ' <= ch && ch < '\x7f') {
          printf("%c", ch);
        } else {
          printf(".");
        }
      } else {
        printf(" ");
      }
    }
    printf("\n");
  }
}

// A frame visitor that will pretty-print frames to stdout.
class FramePrettyPrinter : public spdy::SpdyFramerVisitorInterface {
 public:
  explicit FramePrettyPrinter(spdy::SpdyFramer* framer) : framer_(framer) {}

  virtual void OnError(spdy::SpdyFramer* framer) {
    printf("SpdyFramer encountered an error.\n");
  }

  virtual void OnControl(const spdy::SpdyControlFrame* frame) {
    // Display frame type.
    const spdy::SpdyControlType type = frame->type();
    const spdy::SpdyStreamId stream_id = frame->stream_id();
    printf("Control frame (stream %d) (type %d %s)",
           static_cast<int>(stream_id), static_cast<int>(type),
           (type == spdy::SYN_STREAM ? "SYN_STREAM" :
            type == spdy::SYN_REPLY ? "SYN_REPLY" :
            "<unknown>"));
    // Display priority (if applicable).
    if (type == spdy::SYN_STREAM) {
      printf(" (priority=%d)", static_cast<int>(
          reinterpret_cast<const spdy::SpdySynStreamControlFrame*>(frame)->
          priority()));
    }
    // Display flags.
    int flags = frame->flags();
    printf(" (flags:");
    if (flags) {
      if (flags & spdy::CONTROL_FLAG_FIN) {
        printf(" FIN");
        pending_streams_.erase(stream_id);
      }
      if (flags & spdy::CONTROL_FLAG_UNIDIRECTIONAL) {
        printf(" UNIDIRECTIONAL");
      }
    } else {
      printf(" none");
    }
    printf(")\n");
    // Display headers (if applicable).
    if (type == spdy::SYN_REPLY || type == spdy::SYN_STREAM) {
      spdy::SpdyHeaderBlock headers;
      const bool ok = framer_->ParseHeaderBlock(frame, &headers);
      if (ok) {
        for (spdy::SpdyHeaderBlock::const_iterator iter = headers.begin(),
                 end = headers.end(); iter != end; ++iter) {
          printf("  %s: %s\n", iter->first.c_str(), iter->second.c_str());
        }
      } else {
        printf("  Could not parse headers.\n");
      }
    }
  }

  virtual void OnStreamFrameData(spdy::SpdyStreamId stream_id,
                                 const char* data,
                                 size_t len) {
    if (len == 0) {
      pending_streams_.erase(stream_id);
    } else {
      printf("Data frame (stream %d) (%d bytes)\n",
             static_cast<int>(stream_id),
             static_cast<int>(len));
      const std::string str(data, len);
      HexDump(str);
    }
  }

  // Are we still waiting for any streams to finish?
  bool HasPendingStreams() const {
    return !pending_streams_.empty();
  }

  // Indicate a stream that we initiated.  We keep a set, so that in the future
  // we can request multiple streams at once and wait for them all to finish.
  void PendStream(spdy::SpdyStreamId stream_id) {
    pending_streams_.insert(stream_id);
  }

 private:
  spdy::SpdyFramer* framer_;
  std::set<spdy::SpdyStreamId> pending_streams_;

  DISALLOW_COPY_AND_ASSIGN(FramePrettyPrinter);
};

// Send data over a socket.
bool SendData(int sockfd, const std::string& data) {
  size_t start = 0;
  while (start < data.size()) {
    ssize_t bytes_written = write(sockfd, data.data() + start,
                                  data.size() - start);
    if (bytes_written < 0) {
      return false;
    }
    start += bytes_written;
  }
  return true;
}

}  // namespace

int main(int argc, const char* argv[]) {
  int port = 80;
  if (argc == 3) {
    port = atoi(argv[2]);
  }
  if (argc > 3 || port <= 0) {
    printf("Usage: %s [url] [port]\n"
           "If url is not given, defaults to \"http://localhost/\"\n"
           "If port is not given, defaults to \"80\"\n",
           argv[0]);
    return 1;
  }

  const int sockfd = socket(AF_INET, SOCK_STREAM, 0);
  if (sockfd < 0) {
    printf("Error while opening socket\n");
    return 1;
  }

  struct sockaddr_in serv_addr;
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(port);  // port
  serv_addr.sin_addr.s_addr = INADDR_ANY;  // localhost

  if (connect(sockfd, reinterpret_cast<sockaddr*>(&serv_addr),
              sizeof(serv_addr)) < 0) {
    printf("Error while connecting\n");
    return 1;
  }

  spdy::SpdyFramer request_framer;
  FramePrettyPrinter request_visitor(&request_framer);

  spdy::SpdyFramer response_framer;
  FramePrettyPrinter response_visitor(&response_framer);
  response_framer.set_visitor(&response_visitor);

  std::string url = (argc >= 2 ? argv[1] : "http://localhost/");
  spdy::SpdyStreamId stream_id = 1;

  while (true) {
    // Compose the request data.
    spdy::SpdyHeaderBlock headers;
    headers["method"] = "GET";
    headers["url"] = url;
    headers["version"] = "HTTP/1.1";

    scoped_ptr<spdy::SpdyControlFrame> request_frame(
        request_framer.CreateSynStream(stream_id,  // stream ID
                                       0,  // associated stream ID
                                       1,  // priority
                                       spdy::CONTROL_FLAG_FIN,  // flags
                                       true,  // use compression
                                       &headers));
    printf("\n");
    request_visitor.OnControl(request_frame.get());

    const std::string request_data(request_frame->data(),
                                   spdy::SpdyFrame::size() +
                                   request_frame->length());

    // Send the request data.
    printf("\nSending %d bytes...\n", request_data.size());
    if (!SendData(sockfd, request_data)) {
      printf("Error while writing\n");
      return 1;
    }
    response_visitor.PendStream(stream_id);

    // Receive the response data.
    printf("Awaiting response...\n\n");
    size_t total_response_bytes = 0;
    while (response_visitor.HasPendingStreams()) {
      char buffer[1024];
      const ssize_t bytes_read = read(sockfd, buffer, sizeof(buffer));
      if (bytes_read < 0) {
        printf("Error while reading (%d)\n", bytes_read);
        return 1;
      }
      total_response_bytes += bytes_read;
      response_framer.ProcessInput(buffer, bytes_read);
    }

    printf("\nReceived %d bytes total.\n", total_response_bytes);

    // Rinse, repeat.
    printf("\nNext url (blank for same, Q to quit):\n");
    std::string next_url = ReadLine();
    if (next_url == "Q") {
      break;
    } else if (!next_url.empty()) {
      url = next_url;
    }

    stream_id += 2;
  }

  if (close(sockfd) < 0) {
    printf("Error while closing socket\n");
    return 1;
  }

  return 0;
}
