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


static const char *vertex_shader =
    R"(
#version 300 es

in mediump vec2 position;
in mediump vec2 texcoord;

out mediump vec2 uvpos;

uniform mat4 mvp;

void main() {

   gl_Position = mvp * vec4(position.xy, 0.0, 1.0);
   uvpos = texcoord;
}
)";

// Supplied via ipc
// static const char *fragment_shader =
// R"(
// #version 100
// @builtin_ext@
// @builtin@
//
// precision mediump float;
//
// varying mediump vec2 uvpos;
//
// void main()
// {
// vec4 c = get_pixel(uvpos);
// gl_FragColor = c;
// }
// )";

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

        ~simple_node_render_instance_t()
        {}

        void schedule_instructions(
            std::vector<render_instruction_t>& instructions,
            const wf::render_target_t& target, wf::region_t& damage)
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

            static const float vertexData[] = {
                -1.0f, -1.0f,
                1.0f, -1.0f,
                1.0f, 1.0f,
                -1.0f, 1.0f
            };
            static const float texCoords[] = {
                0.0f, 0.0f,
                1.0f, 0.0f,
                1.0f, 1.0f,
                0.0f, 1.0f
            };

            auto src_tex = wf::gles_texture_t{get_texture(1.0)};
            data.pass->custom_gles_subpass(data.target,[&]
            {
                this->self->shader->use(src_tex.type);
                this->self->shader->attrib_pointer("position", 2, 0, vertexData);
                this->self->shader->attrib_pointer("texcoord", 2, 0, texCoords);
                this->self->shader->uniformMatrix4f("mvp", wf::gles::output_transform(data.target));
                this->self->shader->uniform1f("progress", *self->fade);
                this->self->shader->uniform1i("in_tex", 0);

                if (auto toplevel = wf::toplevel_cast(this->view))
                {
                    auto bg = view->get_surface_root_node()->get_bounding_box();
                    auto vg = toplevel->get_geometry();
                    auto margins =
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

                    this->self->shader->uniform4f("margins", margins);
                }

                GL_CALL(glActiveTexture(GL_TEXTURE0));
                this->self->shader->set_active_texture(src_tex);

                /* Render it to target */
                wf::gles::bind_render_buffer(data.target);
                GL_CALL(glViewport(x, y, w, h));

                GL_CALL(glEnable(GL_BLEND));
                GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

                for (const auto& box : data.damage)
                {
                    wf::gles::render_target_logic_scissor(data.target, wlr_box_from_pixman_box(box));
                    GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
                }

                /* Disable stuff */
                GL_CALL(glDisable(GL_BLEND));
                GL_CALL(glActiveTexture(GL_TEXTURE0));
                GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
                GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

                this->self->shader->deactivate();
            });
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
        wf::gles::run_in_context([&]
        {
            program.compile(vertex_shader, fragment_shader);
        });
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
        wf::gles::run_in_context([&]
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
                wf::gles::run_in_context([&]
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
            wf::gles::run_in_context([&]
            {
                program->free_resources();
            });
        } else
        {
            program = std::make_shared<OpenGL::program_t>();
        }

        std::ifstream t(shader);
        std::string fragment_shader((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
        wf::gles::run_in_context([&]
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

        wf::gles::run_in_context([&]
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
            wf::gles::run_in_context([&]
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
            if (tr->program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
            {
                pop_transformer(view);
                LOGE("Failed to compile shader.");
                return wf::ipc::json_error("Failed to compile shader.");
            }
        } else
        {
            LOGE("Failed to find view with given id. Maybe it isn't mapped?");
            return wf::ipc::json_error("Failed to find view with given id. Maybe it isn't mapped?");
        }

        LOGI("Successfully compiled and applied shader.");
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
