#pragma once

namespace Crucible
{
    class ISystem
    {
    public:

		virtual ~ISystem() = default;

        virtual void Init() = 0;
        virtual void Update() = 0;
        virtual void Shutdown() = 0;
    };
}