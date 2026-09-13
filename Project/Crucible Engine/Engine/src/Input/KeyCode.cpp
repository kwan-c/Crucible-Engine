#include "Input/KeyCode.h"

// Third-party
#include <GLFW/glfw3.h>

namespace Crucible {

    /*
        Generated translation table. This is the ONLY file in the engine that is allowed
        to mention GLFW_KEY_* - that containment is the entire point of KeyCode existing.

        Written as a switch on the macros rather than a table of raw numbers so the
        compiler resolves GLFW's real values; there are no magic constants to get wrong.
    */
    KeyCode KeyCodeFromGlfw(const int& glfwKey) {

        switch (glfwKey) {

        case GLFW_KEY_A:                   return KeyCode::A;
        case GLFW_KEY_B:                   return KeyCode::B;
        case GLFW_KEY_C:                   return KeyCode::C;
        case GLFW_KEY_D:                   return KeyCode::D;
        case GLFW_KEY_E:                   return KeyCode::E;
        case GLFW_KEY_F:                   return KeyCode::F;
        case GLFW_KEY_G:                   return KeyCode::G;
        case GLFW_KEY_H:                   return KeyCode::H;
        case GLFW_KEY_I:                   return KeyCode::I;
        case GLFW_KEY_J:                   return KeyCode::J;
        case GLFW_KEY_K:                   return KeyCode::K;
        case GLFW_KEY_L:                   return KeyCode::L;
        case GLFW_KEY_M:                   return KeyCode::M;
        case GLFW_KEY_N:                   return KeyCode::N;
        case GLFW_KEY_O:                   return KeyCode::O;
        case GLFW_KEY_P:                   return KeyCode::P;
        case GLFW_KEY_Q:                   return KeyCode::Q;
        case GLFW_KEY_R:                   return KeyCode::R;
        case GLFW_KEY_S:                   return KeyCode::S;
        case GLFW_KEY_T:                   return KeyCode::T;
        case GLFW_KEY_U:                   return KeyCode::U;
        case GLFW_KEY_V:                   return KeyCode::V;
        case GLFW_KEY_W:                   return KeyCode::W;
        case GLFW_KEY_X:                   return KeyCode::X;
        case GLFW_KEY_Y:                   return KeyCode::Y;
        case GLFW_KEY_Z:                   return KeyCode::Z;
        case GLFW_KEY_0:                   return KeyCode::Num0;
        case GLFW_KEY_1:                   return KeyCode::Num1;
        case GLFW_KEY_2:                   return KeyCode::Num2;
        case GLFW_KEY_3:                   return KeyCode::Num3;
        case GLFW_KEY_4:                   return KeyCode::Num4;
        case GLFW_KEY_5:                   return KeyCode::Num5;
        case GLFW_KEY_6:                   return KeyCode::Num6;
        case GLFW_KEY_7:                   return KeyCode::Num7;
        case GLFW_KEY_8:                   return KeyCode::Num8;
        case GLFW_KEY_9:                   return KeyCode::Num9;
        case GLFW_KEY_F1:                  return KeyCode::F1;
        case GLFW_KEY_F2:                  return KeyCode::F2;
        case GLFW_KEY_F3:                  return KeyCode::F3;
        case GLFW_KEY_F4:                  return KeyCode::F4;
        case GLFW_KEY_F5:                  return KeyCode::F5;
        case GLFW_KEY_F6:                  return KeyCode::F6;
        case GLFW_KEY_F7:                  return KeyCode::F7;
        case GLFW_KEY_F8:                  return KeyCode::F8;
        case GLFW_KEY_F9:                  return KeyCode::F9;
        case GLFW_KEY_F10:                 return KeyCode::F10;
        case GLFW_KEY_F11:                 return KeyCode::F11;
        case GLFW_KEY_F12:                 return KeyCode::F12;
        case GLFW_KEY_LEFT:                return KeyCode::Left;
        case GLFW_KEY_RIGHT:               return KeyCode::Right;
        case GLFW_KEY_UP:                  return KeyCode::Up;
        case GLFW_KEY_DOWN:                return KeyCode::Down;
        case GLFW_KEY_HOME:                return KeyCode::Home;
        case GLFW_KEY_END:                 return KeyCode::End;
        case GLFW_KEY_PAGE_UP:             return KeyCode::PageUp;
        case GLFW_KEY_PAGE_DOWN:           return KeyCode::PageDown;
        case GLFW_KEY_INSERT:              return KeyCode::Insert;
        case GLFW_KEY_DELETE:              return KeyCode::Delete;
        case GLFW_KEY_SPACE:               return KeyCode::Space;
        case GLFW_KEY_ENTER:               return KeyCode::Enter;
        case GLFW_KEY_TAB:                 return KeyCode::Tab;
        case GLFW_KEY_BACKSPACE:           return KeyCode::Backspace;
        case GLFW_KEY_ESCAPE:              return KeyCode::Escape;
        case GLFW_KEY_APOSTROPHE:          return KeyCode::Apostrophe;
        case GLFW_KEY_COMMA:               return KeyCode::Comma;
        case GLFW_KEY_MINUS:               return KeyCode::Minus;
        case GLFW_KEY_PERIOD:              return KeyCode::Period;
        case GLFW_KEY_SLASH:               return KeyCode::Slash;
        case GLFW_KEY_SEMICOLON:           return KeyCode::Semicolon;
        case GLFW_KEY_EQUAL:               return KeyCode::Equal;
        case GLFW_KEY_LEFT_BRACKET:        return KeyCode::LeftBracket;
        case GLFW_KEY_BACKSLASH:           return KeyCode::Backslash;
        case GLFW_KEY_RIGHT_BRACKET:       return KeyCode::RightBracket;
        case GLFW_KEY_GRAVE_ACCENT:        return KeyCode::GraveAccent;
        case GLFW_KEY_LEFT_SHIFT:          return KeyCode::LeftShift;
        case GLFW_KEY_LEFT_CONTROL:        return KeyCode::LeftControl;
        case GLFW_KEY_LEFT_ALT:            return KeyCode::LeftAlt;
        case GLFW_KEY_LEFT_SUPER:          return KeyCode::LeftSuper;
        case GLFW_KEY_RIGHT_SHIFT:         return KeyCode::RightShift;
        case GLFW_KEY_RIGHT_CONTROL:       return KeyCode::RightControl;
        case GLFW_KEY_RIGHT_ALT:           return KeyCode::RightAlt;
        case GLFW_KEY_RIGHT_SUPER:         return KeyCode::RightSuper;
        case GLFW_KEY_CAPS_LOCK:           return KeyCode::CapsLock;
        case GLFW_KEY_NUM_LOCK:            return KeyCode::NumLock;
        case GLFW_KEY_SCROLL_LOCK:         return KeyCode::ScrollLock;
        case GLFW_KEY_PRINT_SCREEN:        return KeyCode::PrintScreen;
        case GLFW_KEY_PAUSE:               return KeyCode::Pause;
        case GLFW_KEY_KP_0:                return KeyCode::KP0;
        case GLFW_KEY_KP_1:                return KeyCode::KP1;
        case GLFW_KEY_KP_2:                return KeyCode::KP2;
        case GLFW_KEY_KP_3:                return KeyCode::KP3;
        case GLFW_KEY_KP_4:                return KeyCode::KP4;
        case GLFW_KEY_KP_5:                return KeyCode::KP5;
        case GLFW_KEY_KP_6:                return KeyCode::KP6;
        case GLFW_KEY_KP_7:                return KeyCode::KP7;
        case GLFW_KEY_KP_8:                return KeyCode::KP8;
        case GLFW_KEY_KP_9:                return KeyCode::KP9;
        case GLFW_KEY_KP_DECIMAL:          return KeyCode::KPDecimal;
        case GLFW_KEY_KP_DIVIDE:           return KeyCode::KPDivide;
        case GLFW_KEY_KP_MULTIPLY:         return KeyCode::KPMultiply;
        case GLFW_KEY_KP_SUBTRACT:         return KeyCode::KPSubtract;
        case GLFW_KEY_KP_ADD:              return KeyCode::KPAdd;
        case GLFW_KEY_KP_ENTER:            return KeyCode::KPEnter;
        case GLFW_KEY_KP_EQUAL:            return KeyCode::KPEqual;

        default: return KeyCode::Unknown;

        }   // switch

    }   // KeyCodeFromGlfw()

    const char* KeyCodeToString(const KeyCode& key) {

        switch (key) {

        case KeyCode::A:                 return "A";
        case KeyCode::B:                 return "B";
        case KeyCode::C:                 return "C";
        case KeyCode::D:                 return "D";
        case KeyCode::E:                 return "E";
        case KeyCode::F:                 return "F";
        case KeyCode::G:                 return "G";
        case KeyCode::H:                 return "H";
        case KeyCode::I:                 return "I";
        case KeyCode::J:                 return "J";
        case KeyCode::K:                 return "K";
        case KeyCode::L:                 return "L";
        case KeyCode::M:                 return "M";
        case KeyCode::N:                 return "N";
        case KeyCode::O:                 return "O";
        case KeyCode::P:                 return "P";
        case KeyCode::Q:                 return "Q";
        case KeyCode::R:                 return "R";
        case KeyCode::S:                 return "S";
        case KeyCode::T:                 return "T";
        case KeyCode::U:                 return "U";
        case KeyCode::V:                 return "V";
        case KeyCode::W:                 return "W";
        case KeyCode::X:                 return "X";
        case KeyCode::Y:                 return "Y";
        case KeyCode::Z:                 return "Z";
        case KeyCode::Num0:              return "Num0";
        case KeyCode::Num1:              return "Num1";
        case KeyCode::Num2:              return "Num2";
        case KeyCode::Num3:              return "Num3";
        case KeyCode::Num4:              return "Num4";
        case KeyCode::Num5:              return "Num5";
        case KeyCode::Num6:              return "Num6";
        case KeyCode::Num7:              return "Num7";
        case KeyCode::Num8:              return "Num8";
        case KeyCode::Num9:              return "Num9";
        case KeyCode::F1:                return "F1";
        case KeyCode::F2:                return "F2";
        case KeyCode::F3:                return "F3";
        case KeyCode::F4:                return "F4";
        case KeyCode::F5:                return "F5";
        case KeyCode::F6:                return "F6";
        case KeyCode::F7:                return "F7";
        case KeyCode::F8:                return "F8";
        case KeyCode::F9:                return "F9";
        case KeyCode::F10:               return "F10";
        case KeyCode::F11:               return "F11";
        case KeyCode::F12:               return "F12";
        case KeyCode::Left:              return "Left";
        case KeyCode::Right:             return "Right";
        case KeyCode::Up:                return "Up";
        case KeyCode::Down:              return "Down";
        case KeyCode::Home:              return "Home";
        case KeyCode::End:               return "End";
        case KeyCode::PageUp:            return "PageUp";
        case KeyCode::PageDown:          return "PageDown";
        case KeyCode::Insert:            return "Insert";
        case KeyCode::Delete:            return "Delete";
        case KeyCode::Space:             return "Space";
        case KeyCode::Enter:             return "Enter";
        case KeyCode::Tab:               return "Tab";
        case KeyCode::Backspace:         return "Backspace";
        case KeyCode::Escape:            return "Escape";
        case KeyCode::Apostrophe:        return "Apostrophe";
        case KeyCode::Comma:             return "Comma";
        case KeyCode::Minus:             return "Minus";
        case KeyCode::Period:            return "Period";
        case KeyCode::Slash:             return "Slash";
        case KeyCode::Semicolon:         return "Semicolon";
        case KeyCode::Equal:             return "Equal";
        case KeyCode::LeftBracket:       return "LeftBracket";
        case KeyCode::Backslash:         return "Backslash";
        case KeyCode::RightBracket:      return "RightBracket";
        case KeyCode::GraveAccent:       return "GraveAccent";
        case KeyCode::LeftShift:         return "LeftShift";
        case KeyCode::LeftControl:       return "LeftControl";
        case KeyCode::LeftAlt:           return "LeftAlt";
        case KeyCode::LeftSuper:         return "LeftSuper";
        case KeyCode::RightShift:        return "RightShift";
        case KeyCode::RightControl:      return "RightControl";
        case KeyCode::RightAlt:          return "RightAlt";
        case KeyCode::RightSuper:        return "RightSuper";
        case KeyCode::CapsLock:          return "CapsLock";
        case KeyCode::NumLock:           return "NumLock";
        case KeyCode::ScrollLock:        return "ScrollLock";
        case KeyCode::PrintScreen:       return "PrintScreen";
        case KeyCode::Pause:             return "Pause";
        case KeyCode::KP0:               return "KP0";
        case KeyCode::KP1:               return "KP1";
        case KeyCode::KP2:               return "KP2";
        case KeyCode::KP3:               return "KP3";
        case KeyCode::KP4:               return "KP4";
        case KeyCode::KP5:               return "KP5";
        case KeyCode::KP6:               return "KP6";
        case KeyCode::KP7:               return "KP7";
        case KeyCode::KP8:               return "KP8";
        case KeyCode::KP9:               return "KP9";
        case KeyCode::KPDecimal:         return "KPDecimal";
        case KeyCode::KPDivide:          return "KPDivide";
        case KeyCode::KPMultiply:        return "KPMultiply";
        case KeyCode::KPSubtract:        return "KPSubtract";
        case KeyCode::KPAdd:             return "KPAdd";
        case KeyCode::KPEnter:           return "KPEnter";
        case KeyCode::KPEqual:           return "KPEqual";

        case KeyCode::Unknown:
        case KeyCode::Count:
        default: return "Unknown";

        }   // switch

    }   // KeyCodeToString()

}   // NAMESPACE CRUCIBLE
