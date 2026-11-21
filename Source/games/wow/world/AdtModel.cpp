/*
 * AdtModel.cpp  (port of Wow/World/AdtModel.cs: the result types are data, defined in AdtModel.h)
 */

#include "AdtModel.h"

namespace wmv
{
namespace world
{
const char* ToString(AdtPartState s)
{
  switch (s)
  {
    case AdtPartState::NotNamed: return "NotNamed";
    case AdtPartState::NotSupplied: return "NotSupplied";
    case AdtPartState::Parsed: return "Parsed";
    case AdtPartState::Malformed: return "Malformed";
  }
  return "?";
}
} // namespace world
} // namespace wmv
