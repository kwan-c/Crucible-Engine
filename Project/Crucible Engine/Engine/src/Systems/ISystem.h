
namespace Crucible
{
    class ISystem
    {
        virtual void Init() = 0;
        virtual void Update() = 0;
        virtual void Shutdown() = 0;
    };
}