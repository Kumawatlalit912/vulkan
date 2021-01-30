#include "sys.h"
#include "xmlrpc_initialize.h"
#include "utils/macros.h"
#include "utils/AIAlert.h"
#include <boost/iostreams/device/array.hpp>
#include <boost/iostreams/stream.hpp>
#include <cctype>       // std::isspace
#include "debug.h"

namespace xmlrpc {

void initialize(Vector3d& vec, std::string_view const& vector3d_data)
{
  // The format of the string is "[r0.8928223,r0.450409,r0]".
  // Hence, Vector3d does not need xml unescaping, since it does not contain any of '"<>&.
  boost::iostreams::stream<boost::iostreams::basic_array_source<char>> stream(vector3d_data.begin(), vector3d_data.end());
  bool parse_error = false;
  char expected = '[';
  char c;
  int i = 0;
  for (;;)
  {
    // Read next expected character.
    stream >> c;
    if (AI_UNLIKELY(stream.eof()) || AI_UNLIKELY(c != expected))
    {
      // Allow white space in front of expected characters.
      // This therefore allows " [ r0.8928223 , r0.450409, r0 ]".
      if (std::isspace(c))
        continue;
      parse_error = true;
      break;
    }
    if (expected == '[')
      expected = 'r';
    else if (expected == ',')
    {
      ++i;
      expected = 'r';
    }
    else if (expected == 'r')
    {
      expected = (i == 2) ? ']' : ',';
      stream >> vec[i];
    }
    else
      break;
  }
  if (AI_UNLIKELY(parse_error))
    THROW_FALERT("Parse error while decoding \"[DATA]\"", AIArgs("[DATA]", vector3d_data));
}

} // namespace xmlrpc
