#include <cstdint>
#include <string_view>

namespace nyla
{

// TODO: how do we do this

class Path
{
  public:
    void Append(std::string_view segment)
    {
    }

  private:
    void *m_Buf;
    void *m_At;
    uint64_t m_BytesLeft;
};

} // namespace nyla
