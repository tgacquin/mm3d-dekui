// SPDX-License-Identifier: GPL-2.0-or-later
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <nihstro/inline_assembly.h>
#include "mm3d/cpu_interpolation_capture.h"
#include "video_core/pica/cpu_vertex_replay.h"

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
// Previous replay implementation kept as a differential correctness reference.
// It intentionally copies the shader and allocates outputs on every replay.
void ReferenceReplay(Pica::ShaderEngine& engine, const Pica::ShaderSetup& native_setup,
                     const Pica::RegsInternal& regs, const Pica::Uniforms& uniforms,
                     const std::vector<Pica::AttributeBuffer>& inputs,
                     const std::vector<u32>& order, Pica::PipelineRegs::TriangleTopology topology,
                     const Pica::PrimitiveAssembler::TriangleHandler& handler) {
    for (const auto index : order) Check(index < inputs.size(), "Reference index valid");
    auto setup = native_setup;
    setup.uniforms = uniforms;
    engine.SetupBatch(setup, regs.vs.main_offset);
    Pica::ShaderUnit unit;
    std::vector<Pica::OutputVertex> outputs;
    outputs.reserve(inputs.size());
    for (const auto& input : inputs) {
        Pica::AttributeBuffer output{};
        unit.LoadInput(regs.vs, input);
        engine.Run(setup, unit);
        unit.WriteOutput(regs.vs, output);
        outputs.emplace_back(regs.rasterizer, output);
    }
    Pica::PrimitiveAssembler assembler{topology};
    for (const auto index : order) assembler.SubmitVertex(outputs[index], handler);
}

void Run(bool jit) {
    using namespace Pica;
    using Topology = PipelineRegs::TriangleTopology;
    using Op = nihstro::OpCode::Id;
    const auto binary = nihstro::InlineAsm::CompileToRawBinary({
        {Op::MUL, nihstro::DestRegister::MakeTemporary(0),
                  nihstro::SourceRegister::MakeFloat(20), nihstro::SourceRegister::MakeInput(3)},
        {Op::ADD, nihstro::DestRegister::MakeOutput(0),
                  nihstro::SourceRegister::MakeFloat(0), nihstro::SourceRegister::MakeTemporary(0)},
        {Op::END},
    });
    ShaderSetup setup;
    for (unsigned i = 0; i < binary.program.size(); ++i) setup.UpdateProgramCode(i, binary.program[i].hex);
    for (unsigned i = 0; i < binary.swizzle_table.size(); ++i) setup.UpdateSwizzleData(i, binary.swizzle_table[i].hex);
    setup.uniforms = {};
    setup.uniforms.f[20] = Common::Vec4<f24>::AssignToAll(f24::One());
    RegsInternal regs{};
    regs.vs.max_input_attribute_index.Assign(0);
    // Exercise non-identity input routing: attribute zero is loaded to v3.
    regs.vs.input_attribute_to_register_map_low = 3;
    regs.vs.output_mask.Assign(1);
    regs.rasterizer.vs_output_total.Assign(1);
    using Semantic = RasterizerRegs::VSOutputAttributes;
    regs.rasterizer.vs_output_attributes[0].map_x.Assign(Semantic::POSITION_X);
    regs.rasterizer.vs_output_attributes[0].map_y.Assign(Semantic::POSITION_Y);
    regs.rasterizer.vs_output_attributes[0].map_z.Assign(Semantic::POSITION_Z);
    regs.rasterizer.vs_output_attributes[0].map_w.Assign(Semantic::POSITION_W);
    auto engine = CreateEngine(jit);
    engine->SetupBatch(setup, 0);
    const auto original_cached = setup.cached_shader;
    const auto original_hash = setup.GetProgramCodeHash();
    auto midpoint = setup.uniforms;
    midpoint.f[20] = Common::Vec4<f24>::AssignToAll(f24::FromFloat32(2));
    midpoint.f[0].x = f24::FromFloat32(10);

    for (const auto topology : {Topology::List, Topology::Strip, Topology::Fan}) {
        CpuVertexReplay batch;
        const std::vector<u32> order = topology == Topology::List
            ? std::vector<u32>{0,1,2,2,1,3} : std::vector<u32>{0,1,2,3};
        batch.Begin(static_cast<u32>(order.size()), topology);
        for (unsigned i = 0; i < 4; ++i) {
            AttributeBuffer input{};
            input[0] = {f24::FromFloat32(float(i)), f24::Zero(), f24::Zero(), f24::One()};
            Check(batch.AddInput(input) == i, "Input identity retained");
            input[0].x = f24::FromFloat32(999); // Must not change the saved draw.
        }
        for (unsigned i = 0; i + 1 < order.size(); ++i) batch.AddVertex(order[i]);
        Check(!batch.Ready(), "Incomplete captures cannot replay");
        batch.AddVertex(order.back());
        Check(batch.Ready(), "Complete capture available");
        std::vector<float> native, modified;
        const auto append = [](std::vector<float>& out) {
            return [&out](const OutputVertex& a, const OutputVertex& b, const OutputVertex& c) {
                out.insert(out.end(), {a.pos.x.ToFloat32(), b.pos.x.ToFloat32(), c.pos.x.ToFloat32()});
            };
        };
        Check(batch.Replay(*engine, setup, regs, setup.uniforms, append(native)), "Native replay runs");
        Check(batch.Replay(*engine, setup, regs, midpoint, append(modified)), "Midpoint replay runs");
        const std::vector<float> expected = topology == Topology::List
            ? std::vector<float>{0,1,2,2,1,3} : topology == Topology::Strip
            ? std::vector<float>{0,1,2,2,1,3} : std::vector<float>{0,1,2,0,2,3};
        Check(native == expected && modified.size() == expected.size(), "Topology and indexed order preserved");
        for (unsigned i = 0; i < expected.size(); ++i)
            Check(std::abs(modified[i] - (2 * expected[i] + 10)) < .0001f,
                  "Camera and bone uniform overrides affect replay vertices");
        std::vector<float> after;
        auto replacements = batch.Inputs();
        for (auto& input : replacements) input[0].x = input[0].x + f24::FromFloat32(3);
        std::vector<float> particles;
        Check(batch.Replay(*engine, setup, regs, midpoint, append(particles), replacements),
              "Particle inputs replay with midpoint uniforms");
        for (unsigned i = 0; i < expected.size(); ++i)
            Check(std::abs(particles[i] - (2 * (expected[i] + 3) + 10)) < .0001f,
                  "Replacement vertices preserve indexed topology and attribute routing");
        replacements.pop_back();
        Check(!batch.Replay(*engine, setup, regs, midpoint, append(particles), replacements),
              "Partial replacement input rejected");
        batch.Replay(*engine, setup, regs, setup.uniforms, append(after));
        Check(after == native, "Replay does not mutate native inputs or uniforms");
        Check(setup.uniforms.f[20].x.ToFloat32() == 1 && setup.uniforms.f[0].x.ToFloat32() == 0 &&
              setup.cached_shader == original_cached && setup.GetProgramCodeHash() == original_hash,
              "Guest shader state stays unchanged");
        batch.Begin(0, topology);
        Check(!batch.Ready(), "No stale replay after reset");
    }
    CpuVertexReplay invalid;
    invalid.Begin(65537, Topology::List);
    Check(!invalid.Ready(), "Oversized capture rejected");
    invalid.Begin(3, Topology::List);
    invalid.AddInput(AttributeBuffer{});
    for (unsigned i = 0; i < 3; ++i) invalid.AddVertex(99);
    Check(!invalid.Replay(*engine, setup, regs, midpoint, [](const auto&, const auto&, const auto&) {
        throw std::runtime_error("Invalid capture emitted geometry");
    }), "Invalid input index rejected before drawing");

    CpuVertexReplay reusable;
    const auto check_reference = [&](ShaderEngine& replay_engine, unsigned count) {
        reusable.Begin(count, Topology::List);
        std::vector<u32> order;
        for (unsigned i = 0; i < count; ++i) {
            AttributeBuffer input{};
            input[0] = {f24::FromFloat32(float(i)), f24::FromFloat32(2),
                        f24::FromFloat32(3), f24::One()};
            order.push_back(reusable.AddInput(input));
            reusable.AddVertex(order.back());
        }
        std::vector<float> expected, actual;
        const auto append = [](std::vector<float>& out) {
            return [&out](const OutputVertex& a, const OutputVertex& b, const OutputVertex& c) {
                for (const auto* vertex : {&a, &b, &c})
                    for (unsigned i = 0; i < 4; ++i) out.push_back(vertex->pos[i].ToFloat32());
            };
        };
        ReferenceReplay(replay_engine, setup, regs, midpoint, reusable.Inputs(), order,
                        Topology::List, append(expected));
        Check(reusable.Replay(replay_engine, setup, regs, midpoint, append(actual)),
              "Reused replay completes");
        Check(actual == expected, "Reused shader/output storage matches original replay");
    };
    for (unsigned count : {3u, 96u, 6u, 3u}) check_reference(*engine, count);
    const auto original_word = setup.GetProgramCode()[0];
    setup.UpdateProgramCode(0, binary.program[1].hex);
    check_reference(*engine, 6); // Dirty program must invalidate the snapshot.
    setup.UpdateProgramCode(0, original_word);
    engine->SetupBatch(setup, 0);
    setup.GetProgramCodeHash();
    setup.GetSwizzleDataHash();
    check_reference(*engine, 6); // Clean but different hash must also invalidate it.
    const auto original_swizzle = setup.GetSwizzleData()[0];
    setup.UpdateSwizzleData(0, original_swizzle ^ (1u << 4));
    check_reference(*engine, 6);
    setup.UpdateSwizzleData(0, original_swizzle);
    engine->SetupBatch(setup, 0);
    setup.GetProgramCodeHash();
    setup.GetSwizzleDataHash();
    check_reference(*engine, 6);
    regs.vs.main_offset.Assign(1);
    check_reference(*engine, 6);
    regs.vs.main_offset.Assign(0);
    auto other_engine = CreateEngine(!jit);
    check_reference(*other_engine, 6);
    check_reference(*engine, 6);

    // UI translations live in f11 rather than the world-model matrices. Use
    // the actual recorder admission policy and execute both native/half poses.
    const auto ui_binary = nihstro::InlineAsm::CompileToRawBinary({
        {Op::ADD, nihstro::DestRegister::MakeOutput(0),
                  nihstro::SourceRegister::MakeFloat(11), nihstro::SourceRegister::MakeInput(3)},
        {Op::END},
    });
    ShaderSetup ui_setup;
    for (unsigned i = 0; i < ui_binary.program.size(); ++i)
        ui_setup.UpdateProgramCode(i, ui_binary.program[i].hex);
    for (unsigned i = 0; i < ui_binary.swizzle_table.size(); ++i)
        ui_setup.UpdateSwizzleData(i, ui_binary.swizzle_table[i].hex);
    ui_setup.uniforms = {};
    using namespace MM3D::ItemSlideInterpolation;
    for (bool opening : {true, false}) {
        const Sample previous{30, 1, 7, Offset(opening, 2, 400), 2, 400, true, opening};
        const Sample current{31, 1, 7, Offset(opening, 3, 400), 3, 400, true, opening};
        Publish(previous);
        Publish(current);
        const auto delta = Between(previous, current);
        Check(bool(delta), "Popup has a midpoint");
        ui_setup.uniforms.f[11].x = f24::FromFloat32(current.x);
        auto ui_half = ui_setup.uniforms;
        ui_half.f[11].x = f24::FromFloat32(current.x + delta->half);
        for (unsigned count : {6u, 96u}) {
            CpuVertexReplay popup;
            Check(MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
                0xac22b79e29209c01ULL, count, 1, 32, 1), "Popup recorder admits UI inputs");
            popup.Begin(count, Topology::List);
            AttributeBuffer input{};
            input[0].w = f24::One();
            const auto index = popup.AddInput(input);
            for (unsigned i = 0; i < count; ++i) popup.AddVertex(index);
            for (const auto* uniforms : {&ui_setup.uniforms, &ui_half, &ui_setup.uniforms}) {
                unsigned emitted = 0;
                const float expected = uniforms == &ui_half
                    ? Offset(opening, 2.5f, 400) : current.x;
                Check(popup.Replay(*engine, ui_setup, regs, *uniforms,
                    [&](const auto& a, const auto& b, const auto& c) {
                        for (const auto* vertex : {&a, &b, &c}) {
                            Check(std::abs(vertex->pos.x.ToFloat32() - expected) < .01f,
                                  "CPU UI vertices follow eased midpoint without changing native pose");
                            ++emitted;
                        }
                    }), "CPU popup replay runs");
                Check(emitted == count, "CPU popup preserves batch size");
            }
        }
    }

    // File-select batches must be admitted without an active Select Item popup
    // or a MessageWindow plate, then execute the same isolated uniform replay.
    namespace F=MM3D::FileSelectInterpolation;
    F::Values file_a{},file_b{};
    file_a[10]={1,3,94,1};file_a[11]={-200,0,0,40};
    file_a[12]={65,0,14,0};file_a[13]={10,1,0,0};
    file_b=file_a;file_b[11][0]=-160;
    F::History file_history;const F::Key file_key{1,2,7,42};
    file_history.Apply(100,file_key,file_a,6,true);
    const auto file_half=file_history.Apply(101,file_key,file_b,6,true);
    Check(file_half&&file_half->x[0]==-180,"File select has intermediate position");
    F::Publish({101,7,42});
    Check(MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
        0xac22b79e29209c01ULL,6,1,101,7),"File select recorder retains CPU inputs");
    ui_setup.uniforms.f[11].x=f24::FromFloat32(file_b[11][0]);
    auto file_uniforms=ui_setup.uniforms;
    file_uniforms.f[11].x=f24::FromFloat32(file_half->x[0]);
    CpuVertexReplay file_replay;file_replay.Begin(6,Topology::List);
    AttributeBuffer file_input{};file_input[0].w=f24::One();
    const auto file_index=file_replay.AddInput(file_input);
    for(unsigned i=0;i<6;++i)file_replay.AddVertex(file_index);
    for(const auto* uniforms:{&ui_setup.uniforms,&file_uniforms,&ui_setup.uniforms}) {
        unsigned emitted=0;
        const float expected=uniforms==&file_uniforms?-180.f:-160.f;
        Check(file_replay.Replay(*engine,ui_setup,regs,*uniforms,
            [&](const auto& a,const auto& b,const auto& c) {
                for(const auto* vertex:{&a,&b,&c}) {
                    Check(vertex->pos.x.ToFloat32()==expected,"CPU file-select replay uses isolated midpoint");
                    ++emitted;
                }
            }),"CPU file-select replay runs");
        Check(emitted==6,"CPU file-select replay preserves geometry");
    }
    // The banner's vertical translation is encoded in f12.x. Exercise that
    // uniform through actual CPU shader execution and verify native isolation.
    const auto vertical_binary=nihstro::InlineAsm::CompileToRawBinary({
        {Op::ADD,nihstro::DestRegister::MakeOutput(0),
            nihstro::SourceRegister::MakeFloat(12),nihstro::SourceRegister::MakeInput(3)},
        {Op::END},
    });
    ShaderSetup vertical_setup;
    vertical_setup.uniforms={};
    for(unsigned i=0;i<vertical_binary.program.size();++i)
        vertical_setup.UpdateProgramCode(i,vertical_binary.program[i].hex);
    for(unsigned i=0;i<vertical_binary.swizzle_table.size();++i)
        vertical_setup.UpdateSwizzleData(i,vertical_binary.swizzle_table[i].hex);
    file_history.Reset();file_a[10]={0,3,94,1};
    file_a[94]={0,0,0,.78125f};file_a[95]={0,0,.25f,0};
    file_b=file_a;file_b[12][0]=77;
    file_history.Apply(100,file_key,file_a,6,true);
    const auto banner_half=file_history.Apply(101,file_key,file_b,6,true);
    Check(banner_half&&banner_half->y[0]==71,"Banner has vertical midpoint");
    vertical_setup.uniforms.f[12].x=f24::FromFloat32(77);
    auto banner_uniforms=vertical_setup.uniforms;
    banner_uniforms.f[12].x=f24::FromFloat32(banner_half->y[0]);
    for(const auto* uniforms:{&vertical_setup.uniforms,&banner_uniforms,&vertical_setup.uniforms}) {
        const float expected=uniforms==&banner_uniforms?71.f:77.f;
        unsigned emitted=0;
        Check(file_replay.Replay(*engine,vertical_setup,regs,*uniforms,
            [&](const auto& a,const auto& b,const auto& c) {
                for(const auto* vertex:{&a,&b,&c}) {
                    Check(vertex->pos.x.ToFloat32()==expected,"CPU banner uses isolated vertical midpoint");
                    ++emitted;
                }
            }),"CPU banner replay runs");
        Check(emitted==6,"CPU banner retains geometry");
    }
    F::Publish({102,7,0});
    Check(!MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
        0xac22b79e29209c01ULL,6,1,102,7),"Leaving file select disables general layout recording");
    // The magic fill must retain inputs without any active menu. Exercise its
    // width in f11.w through CPU execution, then replay the native width again.
    Check(MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
        0xac22b79e29209c01ULL,6,1,102,7,{0,.921875f,0,.015625f},{0,0,.03125f,0}),
        "CPU retains the magic meter atlas outside menus");
    ui_setup.uniforms.f[11].w=f24::FromFloat32(49.21875f);
    auto meter_uniforms=ui_setup.uniforms;
    meter_uniforms.f[11].w=f24::FromFloat32(47.65625f);
    for(const auto* uniforms:{&ui_setup.uniforms,&meter_uniforms,&ui_setup.uniforms}) {
        unsigned emitted=0;
        const float expected=uniforms==&meter_uniforms?48.65625f:50.21875f;
        Check(file_replay.Replay(*engine,ui_setup,regs,*uniforms,
            [&](const auto& a,const auto& b,const auto& c) {
                for(const auto* vertex:{&a,&b,&c}) {
                    Check(vertex->pos.w.ToFloat32()==expected,"CPU magic width override stays isolated from native draw");
                    ++emitted;
                }
            }),"CPU magic meter replay runs");
        Check(emitted==6,"CPU magic meter retains both triangles");
    }

    F::Values credits_a{};
    credits_a[10]={1,3,94,-93};credits_a[11]={119,0,0,16};
    credits_a[12]={228.25f,0,18,0};credits_a[13]={0,1,0,0};
    credits_a[93]={1,0,.745098f,1};
    credits_a[94]={0,.75f,0,.0625f};credits_a[95]={0,.140625f,.0703125f,0};
    auto credits_b=credits_a;credits_b[12][0]-=.6171875f;
    F::History credits_history;
    credits_history.ApplyCredits(102,{1,2,7,1},credits_a,6,true);
    const auto credits_half=credits_history.ApplyCredits(103,{1,2,7,1},credits_b,6,true);
    Check(credits_half.has_value(),"Credits tail batch has midpoint outside file select");
    for(unsigned count:{6u,12u,24u,66u,72u,78u})
        Check(MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
            0xac22b79e29209c01ULL,count,-93,103,7),"Credits recorder admits full and tail batches");
    Check(!MM3D::CpuInterpolation::CaptureShader(0x9b343fea686d98e0ULL,
        0xac22b79e29209c01ULL,78,-10,103,7),"Malformed font color reference excluded from recording");
    vertical_setup.uniforms.f[12].x=f24::FromFloat32(credits_b[12][0]);
    auto credits_uniforms=vertical_setup.uniforms;
    credits_uniforms.f[12].x=f24::FromFloat32(credits_half->y[0]);
    for(const auto* uniforms:{&vertical_setup.uniforms,&credits_uniforms,&vertical_setup.uniforms}) {
        const float expected=uniforms==&credits_uniforms?credits_half->y[0]:credits_b[12][0];
        unsigned emitted=0;
        Check(file_replay.Replay(*engine,vertical_setup,regs,*uniforms,
            [&](const auto& a,const auto& b,const auto& c) {
                for(const auto* vertex:{&a,&b,&c}) {
                    Check(std::abs(vertex->pos.x.ToFloat32()-expected)<.005f,
                          "CPU credits replay applies isolated scrolling midpoint");
                    ++emitted;
                }
            }),"CPU credits replay runs");
        Check(emitted==6,"CPU credits replay retains tail geometry");
    }

    std::cout << "PASS CPU vertex replay " << (jit ? "JIT" : "interpreter")
              << ": indexed reuse, list/strip/fan, snapshot lifetime, uniform isolation, reset, bounds, "
                 "shader/swizzle/entry/engine changes, scratch reuse and eased CPU popup vertices\n";
}
}
#include "mm3d/gpu_interpolation_inputs_tests.h"
#include "mm3d/gpu_interpolation_capture_check.h"
int main(int argc,char** argv) {
    try { CheckGpuInputs(); Run(false); Run(true); if(argc==2)CheckGpuCaptures(argv[1]); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
