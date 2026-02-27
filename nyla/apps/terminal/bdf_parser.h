#include <cstdint>
#include <span>

namespace nyla
{

struct BdfGlyph
{
    // const char *name;
    std::array<uint8_t, 2> dwidth;
    std::array<uint8_t, 4> bbx;
    std::span<const char> bitmap;
};

class BdfParser
{
  public:
    void Init(std::span<const char> font)
    {
        m_Data = font;
    }

    auto FindGlyph(uint16_t encoding) -> BdfGlyph;

  private:
    std::span<const char> m_Data;
};

} // namespace nyla