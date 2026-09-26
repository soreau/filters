/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2024 Scott Moreau
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <string>
#include <fstream>
#include <filesystem>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/opengl.hpp>
#include <wayfire/util/duration.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/ipc/ipc-activator.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>

#if WF_HAS_VULKANFX
    #include <wayfire/vulkan.hpp>
// Glslang C Interface headers for GLSL to SPIRV conversion
    #include <glslang/Include/glslang_c_interface.h>
    #include <glslang/Public/resource_limits_c.h>

typedef struct
{
    uint32_t *words;
    size_t size; // number of uint32_t words
} SpirvBinary;
#endif


static const char *vertex_shader =
    R"(#version 310 es
#ifdef VULKAN
#extension GL_ARB_shading_language_include : require
#endif

precision highp float;

#include "texture-transform.vert"

layout(location = 0) in highp vec2 position;
layout(location = 1) in highp vec2 texcoord;

layout(location = 0) out highp vec2 uvpos;

#ifdef VULKAN
layout(push_constant) uniform uniforms { mat4 mvp;
                                         vec2 tex_scale;
                                         vec2 tex_offset;
                                         vec4 margins;
                                         float progress; };
#else
uniform mat4 mvp;
uniform vec2 tex_scale;
uniform vec2 tex_offset;
#endif

void main()
{
    gl_Position = mvp * vec4(position.xy, 0.0, 1.0);
#ifdef VULKAN
    uvpos = transform_texture_uv(texcoord, tex_scale, tex_offset);
#else
    uvpos = texcoord;
#endif
}
)";

/* Fragment shader Supplied via ipc */

static std::string pixdecor_custom_data_name = "wf-decoration-shadow-margin";

class wf_shadow_margin_t : public wf::custom_data_t
{
  public:
    wf::decoration_margins_t get_margins()
    {
        return margins;
    }

    void set_margins(wf::decoration_margins_t margins)
    {
        this->margins = margins;
    }

  private:
    wf::decoration_margins_t margins = {0, 0, 0, 0};
};

namespace wf
{
namespace scene
{
namespace filters
{
const std::string transformer_name = "filters";
#if WF_HAS_VULKANFX
std::array<std::shared_ptr<wf::vk::gpu_buffer_t>, 4> vulkan_vertex_buffer;

class vulkan_state_t : public wf::custom_data_t
{
  public:
    std::shared_ptr<wf::vk::graphics_pipeline_t> pipeline;
};

struct vulkan_push_constants_t
{
    alignas(16) glm::mat4 mvp;
    alignas(8)  glm::vec2 uv_scale;
    alignas(8)  glm::vec2 uv_offset;
    alignas(16) glm::vec4 margins;
    alignas(8)  float progress;
};

struct include_user_data_t
{
    std::string shader_directory;
};

glsl_include_result_t *shader_include_local(void *user_data, const char *header_name,
    const char *include_name, size_t include_depth)
{
    include_user_data_t *include_user_data = (include_user_data_t*)user_data;

    std::ifstream t(include_user_data->shader_directory + '/' + header_name);
    std::string shader_directory((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());

    if (shader_directory.empty())
    {
        return NULL;
    }

    glsl_include_result_t *result = (glsl_include_result_t*)malloc(sizeof(glsl_include_result_t));

    result->header_name   = strdup(header_name);
    result->header_data   = strdup(shader_directory.c_str());
    result->header_length = shader_directory.size();
    return result;
}

glsl_include_result_t *shader_include_system(void *user_data, const char *header_name,
    const char *include_name, size_t include_depth)
{
    return NULL;
}

int shader_free_include_result(void *user_data, glsl_include_result_t *result)
{
    if (result)
    {
        free((void*)result->header_name);
        free((void*)result->header_data);
        free(result);
    }

    return 0;
}

SpirvBinary compile_glsl_to_spirv(glslang_stage_t stage, const char *shader_directory,
    const char *glsl_source)
{
    SpirvBinary result = {.words = NULL, .size = 0};

    glsl_include_callbacks_t include_callbacks =
    {
        .include_system = shader_include_system,
        .include_local  = shader_include_local,
        .free_include_result = shader_free_include_result
    };

    include_user_data_t include_user_data =
    {
        .shader_directory = shader_directory
    };

    // 1. Define the compilation input options
    const glslang_input_t input = {
        .language = GLSLANG_SOURCE_GLSL,
        .stage    = stage,
        .client   = GLSLANG_CLIENT_VULKAN,
        .client_version  = GLSLANG_TARGET_VULKAN_1_3,       // Targeting Vulkan 1.3
        .target_language = GLSLANG_TARGET_SPV,
        .target_language_version = GLSLANG_TARGET_SPV_1_3, // SPIR-V 1.3
        .code = glsl_source,
        .default_version = 100,
        .default_profile = GLSLANG_NO_PROFILE,
        .force_default_version_and_profile = false,
        .forward_compatible = false,
        .messages  = GLSLANG_MSG_DEFAULT_BIT,
        .resource  = glslang_default_resource(), // Default hardware limits
        .callbacks = include_callbacks,
        .callbacks_ctx = &include_user_data,
    };

    // 2. Create the shader object instance
    glslang_shader_t *shader = glslang_shader_create(&input);
    if (!shader)
    {
        fprintf(stderr, "Failed to create glslang shader instance.\n");
        return result;
    }

    // 3. Preprocess and Parse the shader string
    if (!glslang_shader_preprocess(shader, &input) || !glslang_shader_parse(shader, &input))
    {
        fprintf(stderr, "Shader compilation failed!\n");
        fprintf(stderr, "Info log:\n%s\n", glslang_shader_get_info_log(shader));
        fprintf(stderr, "Debug log:\n%s\n", glslang_shader_get_info_debug_log(shader));
        glslang_shader_delete(shader);
        return result;
    }

    // 4. Create a program container and link the shader
    glslang_program_t *program = glslang_program_create();
    glslang_program_add_shader(program, shader);

    if (!glslang_program_link(program, GLSLANG_MSG_SPV_RULES_BIT | GLSLANG_MSG_VULKAN_RULES_BIT))
    {
        fprintf(stderr, "Shader linking failed!\n");
        fprintf(stderr, "Program info log:\n%s\n", glslang_program_get_info_log(program));
        glslang_program_delete(program);
        glslang_shader_delete(shader);
        return result;
    }

    // 5. Generate the actual SPIR-V bytecode assembly
    glslang_program_SPIRV_generate(program, stage);

    size_t spirv_size = glslang_program_SPIRV_get_size(program);
    if (spirv_size > 0)
    {
        result.words = (uint32_t*)malloc(spirv_size * sizeof(uint32_t));
        result.size  = spirv_size;
        glslang_program_SPIRV_get(program, result.words);
    }

    // 6. Inspect individual component messages if needed, then clean up code allocations
    const char *spirv_messages = glslang_program_SPIRV_get_messages(program);
    if (spirv_messages)
    {
        printf("SPIR-V Generation Messages:\n%s\n", spirv_messages);
    }

    glslang_program_delete(program);
    glslang_shader_delete(shader);

    return result;
}

bool ensure_vk(wf::vulkan_render_state_t& state, const char *shader_directory, const char *fragment_shader)
{
    glslang_initialize_process();
    VkShaderModule vs, fs;
    SpirvBinary vert_binary = compile_glsl_to_spirv(GLSLANG_STAGE_VERTEX, shader_directory, vertex_shader);
    SpirvBinary frag_binary =
        compile_glsl_to_spirv(GLSLANG_STAGE_FRAGMENT, shader_directory, fragment_shader);

    if (vert_binary.words && frag_binary.words)
    {
        LOGI("Successfully generated SPIR-V vertex shader! Size: %zu words (%zu bytes).\n",
            vert_binary.size, vert_binary.size * sizeof(uint32_t));
        LOGI("Successfully generated SPIR-V fragment shader! Size: %zu words (%zu bytes).\n",
            frag_binary.size, frag_binary.size * sizeof(uint32_t));

        vs =
            state.get_context()->load_shader_module(vert_binary.words,
                vert_binary.size * sizeof(uint32_t));
        fs =
            state.get_context()->load_shader_module(frag_binary.words,
                frag_binary.size * sizeof(uint32_t));

        free(vert_binary.words);
        free(frag_binary.words);
    } else
    {
        LOGI("Compilation failed.\n");
        free(vert_binary.words);
        free(frag_binary.words);
        glslang_finalize_process();
        return false;
    }

    glslang_finalize_process();

    wf::vk::pipeline_params_t params{};
    params.shaders = {
        {.stage = VK_SHADER_STAGE_VERTEX_BIT, .shader = vs},
        {.stage = VK_SHADER_STAGE_FRAGMENT_BIT, .shader = fs},
    };

    params.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;
    params.vertex_input_description = {{
        .binding   = 0,
        .stride    = sizeof(float) * 4,
        .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
    }};
    params.vertex_attribute_description = {
        {
            .location = 0,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = 0,
        },
        {
            .location = 1,
            .binding  = 0,
            .format   = VK_FORMAT_R32G32_SFLOAT,
            .offset   = sizeof(float) * 2,
        },
    };

    // One descriptor set for the texture.
    params.descriptor_set_layouts = {wf::vk::pipeline_params_t::texture_descriptor_set_t{}};
    params.push_constants = {
        VkPushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
            .offset     = 0,
            .size = sizeof(vulkan_push_constants_t),
        },
    };

    auto data = std::make_unique<vulkan_state_t>();
    data->pipeline = std::make_shared<wf::vk::graphics_pipeline_t>(state.get_context(), params);
    state.store_data<vulkan_state_t>(std::move(data));

    return true;
}

std::shared_ptr<wf::vk::gpu_buffer_t> find_buffer(
    std::shared_ptr<wf::vk::context_t> ctx, VkDeviceSize total_size)
{
    auto& buffers = vulkan_vertex_buffer;
    for (size_t i = 0; i < buffers.size(); i++)
    {
        auto& buffer = buffers[i];
        // Try to reuse the vertex buffer if possible, to avoid reallocations.
        if (buffer && (buffer->get_size() >= total_size) && (buffer.use_count() == 1))
        {
            return buffers[i];
        }
    }

    buffers[0] = ctx->create_buffer(total_size,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT);
    return buffers[0];
}

#endif

class wf_filters : public wf::scene::view_2d_transformer_t
{
    wayfire_view view;
    wf::output_t *output;
    OpenGL::program_t *shader;
    std::unique_ptr<wf::animation::simple_animation_t> fade;

  public:
    OpenGL::program_t program;

    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<transformer_base_node_t>
    {
        wf::signal::connection_t<node_damage_signal> on_node_damaged =
            [=] (node_damage_signal *ev)
        {
            push_to_parent(ev->region);
        };

        wf_filters *self;
        wayfire_view view;
        damage_callback push_to_parent;

      public:
        simple_node_render_instance_t(wf_filters *self, damage_callback push_damage,
            wayfire_view view) : wf::scene::transformer_render_instance_t<transformer_base_node_t>(self,
                push_damage,
                view->get_output())
        {
            this->self = self;
            this->view = view;
            this->push_to_parent = push_damage;
            self->connect(&on_node_damaged);
        }

        void transform_damage_region(wf::regionf_t& damage) override
        {
            damage |= self->get_bounding_box();
        }

        ~simple_node_render_instance_t()
        {}

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::regionf_t& damage)
        {
            // We want to render ourselves only, the node does not have children
            instructions.push_back(render_instruction_t{
                            .instance = this,
                            .target   = target,
                            .damage   = damage & self->get_bounding_box(),
                        });
        }

        void render(const wf::scene::render_instruction_t& data)
        {
            wlr_box fb_geom =
                data.target.framebuffer_box_from_geometry_box(data.target.geometry);
            auto view_box = data.target.framebuffer_box_from_geometry_box(
                self->get_children_bounding_box());
            view_box.x -= fb_geom.x;
            view_box.y -= fb_geom.y;

            float x = view_box.x, y = view_box.y, w = view_box.width,
                h = view_box.height;

            glm::vec4 margins{};
            if (auto toplevel = wf::toplevel_cast(this->view))
            {
                auto bg = view->get_surface_root_node()->get_bounding_box();
                auto vg = toplevel->get_geometry();
                margins =
                    glm::vec4{vg.x - bg.x, vg.y - bg.y, bg.width - ((vg.x - bg.x) + vg.width),
                    bg.height - ((vg.y - bg.y) + vg.height)};
                if (view->has_data(pixdecor_custom_data_name))
                {
                    auto decoration_margins =
                        view->get_data<wf_shadow_margin_t>(pixdecor_custom_data_name)->get_margins();
                    margins.x += decoration_margins.left;
                    margins.y += decoration_margins.bottom;
                    margins.z += decoration_margins.right;
                    margins.w += decoration_margins.top;
                }

                // XXX: Pad the margins if there are none, so that the shader renders on the surface
                if (bg == vg)
                {
                    margins.x += 2.0;
                    margins.y += 2.0;
                    margins.z += 2.0;
                    margins.w += 2.0;
                }
            }

            wf::gles::run_in_context_if_gles([&]
            {
                static const float vertexData[] = {
                    -1.0f, -1.0f,
                    1.0f, -1.0f,
                    1.0f, 1.0f,
                    -1.0f, 1.0f
                };
                static const float texCoords[] = {
                    0.0f, 1.0f,
                    1.0f, 1.0f,
                    1.0f, 0.0f,
                    0.0f, 0.0f
                };

                auto src_tex = wf::gles_texture_t{get_texture(1.0)};
                data.pass->custom_gles_subpass(data.target, [&]
                {
                    this->self->shader->use(src_tex.type);
                    this->self->shader->attrib_pointer("position", 2, 0, vertexData);
                    this->self->shader->attrib_pointer("texcoord", 2, 0, texCoords);
                    this->self->shader->uniformMatrix4f("mvp", wf::gles::output_transform(data.target));
                    this->self->shader->uniform1f("progress", *self->fade);
                    this->self->shader->uniform1i("in_tex", 0);
                    this->self->shader->uniform4f("margins", margins);

                    GL_CALL(glActiveTexture(GL_TEXTURE0));
                    this->self->shader->set_active_texture(src_tex);

                    /* Render it to target */
                    wf::gles::bind_render_buffer(data.target);
                    GL_CALL(glViewport(x, y, w, h));

                    GL_CALL(glEnable(GL_BLEND));
                    GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

                    wf::gles::for_each_scissor_rect(data.target, (data.damage & data.target.geometry), [&]
                    {
                        GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
                    });

                    /* Disable stuff */
                    GL_CALL(glDisable(GL_BLEND));
                    GL_CALL(glActiveTexture(GL_TEXTURE0));
                    GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
                    GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

                    this->self->shader->deactivate();
                });
            });

#if WF_HAS_VULKANFX
            data.pass->custom_vulkan_subpass([&] (wf::vulkan_render_state_t& state,
                                                  wf::vk::command_buffer_t& cmd_buf)
            {
                auto our_state = *state.get_data<vulkan_state_t>();

                std::vector<float> unified_buffer;
                unified_buffer.push_back(-1.0f);
                unified_buffer.push_back(-1.0f);
                unified_buffer.push_back(0.0f);
                unified_buffer.push_back(0.0f);
                unified_buffer.push_back(-1.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(0.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(-1.0f);
                unified_buffer.push_back(1.0f);
                unified_buffer.push_back(0.0f);

                VkDeviceSize total_size = unified_buffer.size() * sizeof(float);

                auto buffer = find_buffer(state.get_context(), total_size);
                buffer->write(unified_buffer.data(), total_size);

                auto texture  = get_texture(data.target.scale);
                auto tex_dset = state.get_descriptor_pool()->get_descriptor_set(cmd_buf, texture);
                wf::vk::texture_sampling_params_t sampling{texture};

                wf::vk::pipeline_specialization_t specialization{};
                specialization.add_specialization_for_texture(texture);

                auto [layout, _] = cmd_buf.bind_pipeline(our_state.pipeline, data.target, specialization);

                VkViewport viewport{};
                viewport.x     = x;
                viewport.y     = y;
                viewport.width = w;
                viewport.height   = h;
                viewport.minDepth = 0.0f;
                viewport.maxDepth = 1.0f;
                vkCmdSetViewport(cmd_buf, 0, 1, &viewport);

                cmd_buf.bind_texture(texture);

                vkCmdBindDescriptorSets(cmd_buf, VK_PIPELINE_BIND_POINT_GRAPHICS, layout,
                    0, 1, &tex_dset, 0, nullptr);

                vulkan_push_constants_t push_constants{};
                push_constants.mvp = wf::gles::output_transform(data.target);
                push_constants.uv_scale  = sampling.get_uv_scale();
                push_constants.uv_offset = sampling.get_uv_offset();
                push_constants.margins   = margins;
                push_constants.progress  = *self->fade;
                vkCmdPushConstants(cmd_buf, layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                    0, sizeof(vulkan_push_constants_t), &push_constants);

                cmd_buf.bind_buffer(buffer);
                VkDeviceSize offset = 0;
                vkCmdBindVertexBuffers(cmd_buf, 0, 1, &buffer->get_buffer(), &offset);

                cmd_buf.for_each_scissor_rect(data.target, (data.damage & data.target.geometry), [&]
                {
                    vkCmdDraw(cmd_buf, 4, 1, 0, 0);
                });

                cmd_buf.set_full_viewport(data.target);
            });
#endif
        }
    };

    wf_filters(wayfire_view view, std::string shader_path) : wf::scene::view_2d_transformer_t(view)
    {
        this->view   = view;
        this->shader = &program;
        if (view->get_output())
        {
            output = view->get_output();
            output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        }

        std::ifstream t(shader_path);
        std::string fragment_shader((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
        wf::gles::run_in_context_if_gles([&]
        {
            program.compile(vertex_shader, fragment_shader);
        });
#if WF_HAS_VULKANFX
        if (wf::get_core().is_vulkan())
        {
            if (!ensure_vk(vulkan_render_state_t::get(),
                std::filesystem::path(shader_path).parent_path().c_str(), fragment_shader.c_str()))
            {
                pop_transformer(view);
            }
        }

#endif
        fade = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(700));
        fade->set(0.0, 0.0);
        fade->animate(1.0);
    }

    void unapply()
    {
        fade->animate(0.0);
    }

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(transformer_name))
        {
            LOGI("Removing shader and transformer.");
            view->get_transformed_node()->rem_transformer(transformer_name);
        }
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        if (fade->running())
        {
            for (auto & v : wf::get_core().get_all_views())
            {
                v->damage();
            }
        } else if (fade->end == 0.0)
        {
            pop_transformer(view);
        }
    };

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    virtual ~wf_filters()
    {
        wf::gles::run_in_context_if_gles([&]
        {
            program.free_resources();
        });
        fade.reset();
        if (output)
        {
            output->render->rem_effect(&pre_hook);
        }
    }
};

class wayfire_per_output_filters : public wf::per_output_plugin_instance_t
{
    std::unique_ptr<wf::animation::simple_animation_t> fade;
    std::shared_ptr<OpenGL::program_t> program = nullptr;
    wf::post_hook_t hook;
    bool active = false;

  public:
    void init() override
    {
        hook = [=] (wf::auxilliary_buffer_t& aux_buf, const wf::render_buffer_t& render_buf)
        {
            render(aux_buf, render_buf);
        };
        fade = std::make_unique<wf::animation::simple_animation_t>(wf::create_option<int>(700));
        fade->set(0.0, 0.0);
    }

    wf::effect_hook_t pre_hook = [=] ()
    {
        if (fade->running())
        {
            output->render->damage_whole();
            for (auto & v : wf::get_core().get_all_views())
            {
                v->damage();
            }
        } else if (fade->end == 0.0)
        {
            output->render->rem_effect(&pre_hook);
            output->render->rem_post(&hook);
            output->render->damage_whole();
            if (program)
            {
                wf::gles::run_in_context_if_gles([&]
                {
                    program->free_resources();
                });
            }

            program = nullptr;
            active  = false;
        }
    };

    wf::json_t set_fs_shader(std::string shader)
    {
        if (program)
        {
            wf::gles::run_in_context_if_gles([&]
            {
                program->free_resources();
            });
        } else
        {
            program = std::make_shared<OpenGL::program_t>();
        }

        std::ifstream t(shader);
        std::string fragment_shader((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
        wf::gles::run_in_context_if_gles([&]
        {
            program->compile(vertex_shader, fragment_shader);
        });
        if (program->get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
        {
            LOGE("Failed to compile fullscreen shader.");
            output->render->rem_post(&hook);
            program = nullptr;
            return wf::ipc::json_error("Failed to compile fullscreen shader.");
        }

        output->render->damage_whole();

        if (active)
        {
            LOGI("Successfully compiled and applied fullscreen shader to output: ", output->to_string());
            return wf::ipc::json_ok();
        }

        output->render->add_post(&hook);
        output->render->add_effect(&pre_hook, wf::OUTPUT_EFFECT_PRE);
        fade->animate(1.0);
        active = true;

        LOGI("Successfully compiled and applied fullscreen shader to output: ", output->to_string());
        return wf::ipc::json_ok();
    }

    wf::json_t unset_fs_shader()
    {
        fade->animate(0.0);
        return wf::ipc::json_ok();
    }

    wf::json_t fs_has_shader()
    {
        auto response = wf::ipc::json_ok();
        response["has-shader"] = active;
        return response;
    }

    void render(wf::auxilliary_buffer_t& aux_buf, const wf::render_buffer_t& render_buf)
    {
        static const float vertexData[] = {
            -1.0f, -1.0f,
            1.0f, -1.0f,
            1.0f, 1.0f,
            -1.0f, 1.0f
        };
        static const float texCoords[] = {
            0.0f, 1.0f,
            1.0f, 1.0f,
            1.0f, 0.0f,
            0.0f, 0.0f
        };

        wf::gles::run_in_context_if_gles([&]
        {
            /* Upload data to shader */
            program->use(wf::TEXTURE_TYPE_RGBA);
            program->attrib_pointer("position", 2, 0, vertexData);
            program->attrib_pointer("texcoord", 2, 0, texCoords);
            program->uniformMatrix4f("mvp", glm::mat4(1.0));
            program->uniform1f("progress", *fade);
            program->uniform1i("in_tex", 0);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            program->set_active_texture(wf::gles_texture_t::from_aux(aux_buf));

            /* Render it to aux_buf */
            wf::gles::bind_render_buffer(render_buf);
            GL_CALL(glViewport(0, 0, aux_buf.get_size().width, aux_buf.get_size().height));

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

            GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));

            /* Disable stuff */
            GL_CALL(glDisable(GL_BLEND));
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

            program->deactivate();
        });
    }

    void fini() override
    {
        output->render->rem_effect(&pre_hook);
        output->render->rem_post(&hook);
        output->render->damage_whole();
        if (program)
        {
            wf::gles::run_in_context_if_gles([&]
            {
                program->free_resources();
            });
        }

        fade.reset();
    }
};

class wayfire_filters : public wf::plugin_interface_t,
    public wf::per_output_tracker_mixin_t<wayfire_per_output_filters>
{
    wf::shared_data::ref_ptr_t<wf::ipc::method_repository_t> ipc_repo;

    void pop_transformer(wayfire_view view)
    {
        if (view->get_transformed_node()->get_transformer(transformer_name))
        {
            LOGI("Removing shader and transformer.");
            view->get_transformed_node()->rem_transformer(transformer_name);
        }
    }

    void remove_transformers()
    {
        for (auto& view : wf::get_core().get_all_views())
        {
            pop_transformer(view);
        }
    }

  public:
    void init() override
    {
        ipc_repo->register_method("wf/filters/set-view-shader", ipc_set_view_shader);
        ipc_repo->register_method("wf/filters/unset-view-shader", ipc_unset_view_shader);
        ipc_repo->register_method("wf/filters/view-has-shader", ipc_view_has_shader);
        ipc_repo->register_method("wf/filters/set-fs-shader", ipc_set_fs_shader);
        ipc_repo->register_method("wf/filters/unset-fs-shader", ipc_unset_fs_shader);
        ipc_repo->register_method("wf/filters/fs-has-shader", ipc_fs_has_shader);

        per_output_tracker_mixin_t::init_output_tracking();
    }

    void handle_new_output(wf::output_t *output) override
    {
        per_output_tracker_mixin_t::handle_new_output(output);
    }

    void handle_output_removed(wf::output_t *output) override
    {
        per_output_tracker_mixin_t::handle_output_removed(output);
    }

    std::shared_ptr<wf_filters> ensure_transformer(wayfire_view view, std::string shader_path)
    {
        auto tmgr = view->get_transformed_node();
        if (tmgr->get_transformer<wf_filters>(transformer_name))
        {
            view->get_transformed_node()->rem_transformer(transformer_name);
        }

        auto node = std::make_shared<wf_filters>(view, shader_path);
        tmgr->add_transformer(node, wf::TRANSFORMER_2D, transformer_name);

        return tmgr->get_transformer<wf_filters>(transformer_name);
    }

    wf::ipc::method_callback ipc_set_view_shader = [=] (wf::json_t data) -> wf::json_t
    {
        auto view_id     = wf::ipc::json_get_uint64(data, "view-id");
        auto shader_path = wf::ipc::json_get_string(data, "shader-path");

        auto view = wf::ipc::find_view_by_id(view_id);
        if (view)
        {
            auto tr = ensure_transformer(view, shader_path);
            wf::gles::run_in_context_if_gles([&]
            {
                if (tr->program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
                {
                    pop_transformer(view);
                    LOGE("Failed to compile shader.");
                    return wf::ipc::json_error("Failed to compile shader.");
                }

                LOGI("Successfully compiled and applied shader.");
                view->damage();
                return wf::ipc::json_ok();
            });
            LOGI("Transformer applied.");
        } else
        {
            LOGE("Failed to find view with given id. Maybe it isn't mapped?");
            return wf::ipc::json_error("Failed to find view with given id. Maybe it isn't mapped?");
        }

        // LOGI("Successfully compiled and applied shader.");
        view->damage();
        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback ipc_unset_view_shader = [=] (wf::json_t data) -> wf::json_t
    {
        auto view_id = wf::ipc::json_get_uint64(data, "view-id");

        auto view = wf::ipc::find_view_by_id(view_id);
        if (view)
        {
            auto tmgr = view->get_transformed_node();
            if (auto tr = tmgr->get_transformer<wf_filters>(transformer_name))
            {
                tr->unapply();
                view->damage();
            }
        }

        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback ipc_view_has_shader = [=] (wf::json_t data) -> wf::json_t
    {
        auto view_id = wf::ipc::json_get_uint64(data, "view-id");

        auto view = wf::ipc::find_view_by_id(view_id);
        if (!view)
        {
            return wf::ipc::json_error("Failed to find view with given id.");
        }

        auto tmgr     = view->get_transformed_node();
        auto response = wf::ipc::json_ok();
        response["has-shader"] = tmgr->get_transformer<wf::scene::node_t>(transformer_name) ? true : false;
        return response;
    };

    wf::output_t *find_output_by_name(std::string name)
    {
        for (auto & output : wf::get_core().output_layout->get_outputs())
        {
            if (output->to_string() == name)
            {
                return output;
            }
        }

        return nullptr;
    }

    wf::ipc::method_callback ipc_set_fs_shader = [=] (wf::json_t data) -> wf::json_t
    {
        auto output_name = wf::ipc::json_get_string(data, "output-name");
        auto shader_path = wf::ipc::json_get_string(data, "shader-path");

        auto output = find_output_by_name(output_name);
        if (!output)
        {
            return wf::ipc::json_error("No such output");
        }

        return this->output_instance[output]->set_fs_shader(shader_path);
    };

    wf::ipc::method_callback ipc_unset_fs_shader = [=] (wf::json_t data) -> wf::json_t
    {
        auto output_name = wf::ipc::json_get_string(data, "output-name");

        auto output = find_output_by_name(output_name);
        if (!output)
        {
            return wf::ipc::json_error("No such output");
        }

        return this->output_instance[output]->unset_fs_shader();
    };

    wf::ipc::method_callback ipc_fs_has_shader = [=] (wf::json_t data) -> wf::json_t
    {
        auto output_name = wf::ipc::json_get_string(data, "output-name");

        auto output = find_output_by_name(output_name);
        if (!output)
        {
            return wf::ipc::json_error("No such output");
        }

        return this->output_instance[output]->fs_has_shader();
    };

    void fini() override
    {
        per_output_tracker_mixin_t::fini_output_tracking();

        ipc_repo->unregister_method("wf/filters/set-view-shader");
        ipc_repo->unregister_method("wf/filters/unset-view-shader");
        ipc_repo->unregister_method("wf/filters/view-has-shader");
        ipc_repo->unregister_method("wf/filters/set-fs-shader");
        ipc_repo->unregister_method("wf/filters/unset-fs-shader");
        ipc_repo->unregister_method("wf/filters/fs-has-shader");

        remove_transformers();
    }
};
}
}
}

DECLARE_WAYFIRE_PLUGIN(wf::scene::filters::wayfire_filters);
