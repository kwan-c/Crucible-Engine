#pragma once

namespace Crucible {

    class Layer {

    public:
    
        virtual ~Layer() = default;
        virtual void OnAttach() {}
        virtual void OnUpdate(float deltaTime) {}
        virtual void OnRender() {}
        virtual void OnDetach() {}

    };

}