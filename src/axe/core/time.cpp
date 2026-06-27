#include "time.hpp"

namespace axe
{
    // Definição do static dentro da axe.dll — fonte de verdade única.
    float Time::s_Elapsed = 0.0f;

    float Time::Elapsed()
    {
        return s_Elapsed;
    }

    void Time::SetElapsed(float seconds)
    {
        s_Elapsed = seconds;
    }
}