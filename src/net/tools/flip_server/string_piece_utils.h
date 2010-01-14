// The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_TOOLS_FLIP_SERVER_STRING_PIECE_UTILS_H_
#define NET_TOOLS_FLIP_SERVER_STRING_PIECE_UTILS_H_

#include <ctype.h>

#include "base/port.h"
#include "base/string_piece.h"
#include "base/string_util.h"

namespace net {

struct StringPieceCaseHash {
  size_t operator()(const base::StringPiece& sp) const {
    // based on __stl_string_hash in http://www.sgi.com/tech/stl/string
    size_t hash_val = 0;
    for (base::StringPiece::const_iterator it = sp.begin();
         it != sp.end(); ++it) {
      hash_val = 5 * hash_val + tolower(*it);
    }
    return hash_val;
  }
};

struct StringPieceUtils {
  static bool EqualIgnoreCase(const base::StringPiece& piece1,
                              const base::StringPiece& piece2) {
    return (piece1.size() == piece2.size()
        && 0 == base::strncasecmp(piece1.data(),
                                  piece2.data(),
                                  piece1.size()));
  }

  static void RemoveWhitespaceContext(base::StringPiece* piece1) {
    base::StringPiece::const_iterator c = piece1->begin();
    base::StringPiece::const_iterator e = piece1->end();
    while (c != e && isspace(*c)) {
      ++c;
    }
    if (c == e) {
      *piece1 = base::StringPiece(c, e-c);
      return;
    }
    --e;
    while (c != e &&isspace(*e)) {
      --e;
    }
    ++e;
    *piece1 = base::StringPiece(c, e-c);
  }

  static bool StartsWithIgnoreCase(const base::StringPiece& text,
                                   const base::StringPiece& starts_with) {
    if (text.size() < starts_with.size())
      return false;
    return EqualIgnoreCase(text.substr(0, starts_with.size()), starts_with);
  }
};
struct StringPieceCaseEqual {
  bool operator()(const base::StringPiece& piece1,
                  const base::StringPiece& piece2) const {
    return StringPieceUtils::EqualIgnoreCase(piece1, piece2);
  }
};

}  // namespace net

#endif  // NET_TOOLS_FLIP_SERVER_STRING_PIECE_UTILS_H_
