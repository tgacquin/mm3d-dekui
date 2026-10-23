// SPDX-License-Identifier: GPL-2.0-or-later
// This console test owns its entry point instead of linking SDL2main.
#define SDL_MAIN_HANDLED
#include <SDL.h>
#include <iostream>
#include <stdexcept>
#include "mm3d/controller_autoconfig.h"
#include "input_common/sdl/sdl_impl.h"

static void Check(bool value,const char* message) {
    if(!value)throw std::runtime_error(message);
}
int main() {
    try {
        namespace A=MM3D::ControllerAutoConfig;
        Settings::InputProfile original{};original.name="Default";
        original.motion_device="engine:motion_emu,update_period:100,sensitivity:0.01,tilt_clamp:90.0";
        original.touch_device="engine:emu_window";
        for(unsigned i=0;i<original.buttons.size();++i)original.buttons[i]="keyboard"+std::to_string(i);
        for(unsigned i=0;i<original.analogs.size();++i)original.analogs[i]="analog"+std::to_string(i);
        Check(A::Untouched(original,original.buttons,original.analogs),"untouched defaults eligible");
        for(unsigned i=0;i<original.buttons.size();++i) {
            auto p=original;p.buttons[i]="custom";
            Check(!A::Untouched(p,original.buttons,original.analogs),"custom button preserved");
        }
        for(unsigned i=0;i<original.analogs.size();++i) {
            auto p=original;p.analogs[i]="custom";
            Check(!A::Untouched(p,original.buttons,original.analogs),"custom stick preserved");
        }
        for(unsigned i=0;i<5;++i) {
            auto p=original;
            if(i==0)p.name="My keyboard";
            if(i==1)p.motion_device="engine:cemuhookudp";
            if(i==2)p.use_touchpad=true;
            if(i==3)p.use_touch_from_button=true;
            if(i==4)p.maptype=Settings::InputMappingType::AllControllers;
            Check(A::Untouched(p,original.buttons,original.analogs)==(i!=4),"unrelated preferences do not prevent mapping; custom mapping mode preserved");
        }
        SDL_SetMainReady();
        Check(SDL_Init(SDL_INIT_GAMECONTROLLER)==0,"SDL initialization");
        {
            InputCommon::SDL::SDLState state;
            const auto before=state.ConnectedControllers().size();
            const int pad=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
            Check(pad>=0,"virtual gamepad attached");SDL_PumpEvents();
            const auto id=SDL_JoystickGetDeviceInstanceID(pad);
            auto pads=state.ConnectedControllers();Check(pads.size()==before+1,"hotplug discovered");
            const auto candidate=pads.back();
            // A centered stick must not steal a button mapping with a false release.
            auto pollers=state.GetPollers(InputCommon::Polling::DeviceType::Button);
            auto& poller=*pollers.front();
            poller.Start();
            auto axis_event=[&](Sint16 value) {
                SDL_Event event{};event.type=SDL_CONTROLLERAXISMOTION;
                event.caxis.which=id;event.caxis.axis=SDL_CONTROLLER_AXIS_LEFTX;
                event.caxis.value=value;event.caxis.timestamp=1000;
                state.event_queue.Push(event);
                return poller.GetNextInput();
            };
            Check(!axis_event(0).Has("engine"),"initial stick center ignored");
            Check(!axis_event(-100).Has("engine"),"center jitter must not emit an axis release");
            Check(!axis_event(100).Has("engine"),"continued center jitter ignored");
            auto axis_down=axis_event(-25000);
            Check(axis_down.Has("down")&&axis_down.Get("direction","")=="-","deliberate stick movement still captured");
            auto axis_up=axis_event(0);
            Check(axis_up.Has("axis")&&!axis_up.Has("down"),"deliberate stick release still captured");
            SDL_Event button_event{};button_event.type=SDL_CONTROLLERBUTTONDOWN;
            button_event.cbutton.which=id;button_event.cbutton.button=SDL_CONTROLLER_BUTTON_B;
            button_event.cbutton.timestamp=2000;state.event_queue.Push(button_event);
            Check(poller.GetNextInput().Has("down"),"B press captured as button down");
            button_event.type=SDL_CONTROLLERBUTTONUP;state.event_queue.Push(button_event);
            auto released=poller.GetNextInput();
            Check(released.Get("button",-1)==SDL_CONTROLLER_BUTTON_B&&!released.Has("axis")&&
                !released.Has("down"),"B release maps B after centered stick noise");
            poller.Stop();

            Check(candidate.Get("engine","")=="sdl"&&candidate.Get("api","")=="controller","recognized gamepad");
            const auto buttons=[&](const Common::ParamPackage& p,int i){return state.GetSDLControllerButtonBind(p,static_cast<Settings::NativeButton::Values>(i));};
            const auto analogs=[&](const Common::ParamPackage& p,int i){return state.GetSDLControllerAnalogBindByGUID(p.Get("guid",""),p.Get("port",0),static_cast<Settings::NativeAnalog::Values>(i));};
            auto mapped=original;Check(A::Map(mapped,candidate,buttons,analogs),"full auto map succeeds");
            Check(A::Managed(mapped,buttons,analogs),"automatic binding identity recognized");
            auto second=candidate;second.Set("guid","different-controller");
            auto recovery=A::Select(mapped,{second},original.buttons,original.analogs,buttons,analogs);
            Check(recovery&&Common::ParamPackage(recovery->buttons[0]).Get("guid","")=="different-controller","disconnected saved controller replaced");
            Check(!A::Select(mapped,{second,candidate},original.buttons,original.analogs,buttons,analogs),"connected saved controller retained without activity");
            second.Set("active",1);
            Check(A::Select(mapped,{candidate,second},original.buttons,original.analogs,buttons,analogs).has_value(),"button-active second controller wins");
            Check(!A::Select(mapped,{},original.buttons,original.analogs,buttons,analogs),"disconnect without replacement retains bindings");
            auto customized=mapped;customized.buttons[0]="engine:keyboard,code:99";
            Check(!A::Managed(customized,buttons,analogs)&&!A::Select(customized,{second},original.buttons,original.analogs,buttons,analogs),"manually edited auto profile protected");
            customized=mapped;customized.analogs[0]+=",deadzone:0.2";
            Check(!A::Select(customized,{second},original.buttons,original.analogs,buttons,analogs),"custom stick tuning protected");
            auto peripheral=original;peripheral.motion_device="engine:cemuhookudp";peripheral.use_touchpad=true;
            auto mapped_peripheral=A::Select(peripheral,{candidate},original.buttons,original.analogs,buttons,analogs);
            Check(mapped_peripheral&&mapped_peripheral->motion_device==peripheral.motion_device&&mapped_peripheral->use_touchpad,"motion and touch preferences retained");
            auto incomplete=candidate;incomplete.Set("guid","incomplete");
            const auto guarded_analogs=[&](const auto& p,int i){return p.Get("guid","")=="incomplete"?Common::ParamPackage{}:analogs(p,i);};
            Check(A::Select(original,{incomplete,candidate},original.buttons,original.analogs,buttons,guarded_analogs).has_value(),"incomplete first device does not block later devices");
            SDL_Event press{};press.type=SDL_CONTROLLERBUTTONDOWN;press.cbutton.which=id;press.cbutton.button=SDL_CONTROLLER_BUTTON_A;
            state.HandleGameControllerEvent(press);
            auto active_pads=state.ConnectedControllers();
            Check(std::any_of(active_pads.begin(),active_pads.end(),[&](const auto& p){return A::SameDevice(p,candidate)&&p.Get("active",0)==1;}),"SDL button press publishes active controller");
            Check(!A::Untouched(mapped,original.buttons,original.analogs),"saved map is not remapped");
            const auto button=[&](int i){return Common::ParamPackage(mapped.buttons[i]);};
            Check(button(Settings::NativeButton::A).Get("button",-1)==SDL_CONTROLLER_BUTTON_B,"A maps to right face");
            Check(button(Settings::NativeButton::B).Get("button",-1)==SDL_CONTROLLER_BUTTON_A,"B maps to bottom face");
            Check(button(Settings::NativeButton::LeftStickClick).Get("button",-1)==SDL_CONTROLLER_BUTTON_LEFTSTICK,"left stick click binding");
            Check(button(Settings::NativeButton::RightStickClick).Get("button",-1)==SDL_CONTROLLER_BUTTON_RIGHTSTICK,"right stick click binding");
            for(auto click:{SDL_CONTROLLER_BUTTON_LEFTSTICK,SDL_CONTROLLER_BUTTON_RIGHTSTICK}) {
                poller.Start();
                SDL_Event event{};event.type=SDL_CONTROLLERBUTTONDOWN;event.cbutton.which=id;event.cbutton.button=click;
                state.event_queue.Push(event);Check(poller.GetNextInput().Has("down"),"stick click press captured");
                event.type=SDL_CONTROLLERBUTTONUP;state.event_queue.Push(event);
                auto up=poller.GetNextInput();Check(up.Get("button",-1)==click&&!up.Has("down"),"stick click release captured");
                poller.Stop();
            }
            Check(button(Settings::NativeButton::Start).Get("button",-1)==SDL_CONTROLLER_BUTTON_START,"Start mapping");
            Check(button(Settings::NativeButton::Select).Get("button",-1)==SDL_CONTROLLER_BUTTON_BACK,"Select mapping");
            Check(button(Settings::NativeButton::ZR).Get("axis",-1)==SDL_CONTROLLER_AXIS_TRIGGERRIGHT,"ZR trigger");
            Check(button(Settings::NativeButton::ZL).Get("axis",-1)==SDL_CONTROLLER_AXIS_TRIGGERLEFT,"ZL trigger");
            Check(Common::ParamPackage(mapped.analogs[0]).Get("axis_x",-1)==SDL_CONTROLLER_AXIS_LEFTX,"movement stick");
            Check(Common::ParamPackage(mapped.analogs[1]).Get("axis_x",-1)==SDL_CONTROLLER_AXIS_RIGHTX,"camera stick");
            Check(mapped.buttons[Settings::NativeButton::Power]==original.buttons[Settings::NativeButton::Power],"unmapped emulator keys retained");
            auto failed=original;
            Check(!A::Map(failed,candidate,buttons,[](auto&,int){return Common::ParamPackage{};}),"incomplete mapping refused");
            Check(failed.buttons==original.buttons&&failed.analogs==original.analogs,"failed map leaves controls unchanged");
            Check(SDL_JoystickDetachVirtual(pad)==0,"disconnect");SDL_PumpEvents();
            Check(state.ConnectedControllers().size()==before,"disconnected gamepad removed");
            const int again=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);SDL_PumpEvents();
            Check(again>=0&&SDL_JoystickGetDeviceInstanceID(again)!=id,"reconnection has new instance");
            pads=state.ConnectedControllers();Check(pads.size()==before+1,"reconnection discovered");
            Check(pads.back().Get("guid","")==candidate.Get("guid","")&&pads.back().Get("port",-1)==candidate.Get("port",-2),"saved binding identity survives reconnect");
            Check(SDL_JoystickDetachVirtual(again)==0,"reconnected gamepad removed");SDL_PumpEvents();
            const int raw=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_FLIGHT_STICK,2,2,0);SDL_PumpEvents();
            Check(raw>=0&&state.ConnectedControllers().size()==before,"unmapped joystick excluded");
            SDL_JoystickDetachVirtual(raw);SDL_PumpEvents();
        }
        // A pad already attached before the input backend starts is also
        // discoverable; setup must not depend on pressing a button first.
        const int startup=SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER,6,15,0);
        Check(startup>=0,"startup gamepad attached");SDL_PumpEvents();
        char guid[33]{};SDL_JoystickGetGUIDString(SDL_JoystickGetDeviceGUID(startup),guid,sizeof(guid));
        {
            InputCommon::SDL::SDLState state;
            const auto pads=state.ConnectedControllers();
            Check(std::any_of(pads.begin(),pads.end(),[&](const auto& p){return p.Get("guid","")==guid;}),"startup controller discovered without button press");
            SDL_JoystickDetachVirtual(startup);SDL_PumpEvents();
        }
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);
        std::cout<<"Controller default protection, mapping, hotplug and reconnect checks passed\n";
        return 0;
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
