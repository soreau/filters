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
#include <streambuf>
#include <wayfire/core.hpp>
#include <wayfire/view.hpp>
#include <wayfire/plugin.hpp>
#include <wayfire/output.hpp>
#include <wayfire/render-manager.hpp>
#include <wayfire/view-transform.hpp>
#include <wayfire/per-output-plugin.hpp>
#include <wayfire/signal-definitions.hpp>
#include <wayfire/plugins/ipc/ipc-helpers.hpp>
#include <wayfire/plugins/common/shared-core-data.hpp>
#include <wayfire/plugins/ipc/ipc-method-repository.hpp>


static const char *vertex_shader =
    R"(
#version 100

attribute mediump vec2 position;
attribute mediump vec2 texcoord;

varying mediump vec2 uvpos;

uniform mat4 mvp;

void main() {

   gl_Position = mvp * vec4(position.xy, 0.0, 1.0);
   uvpos = texcoord;
}
)";

// Supplied via ipc
//static const char *fragment_shader =
//    R"(
//#version 100
//@builtin_ext@
//@builtin@
//
//precision mediump float;
//
//varying mediump vec2 uvpos;
//
//void main()
//{
//    vec4 c = get_pixel(uvpos);
//    gl_FragColor = c;
//}
//)";

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
    OpenGL::program_t *shader;

  public:
    OpenGL::program_t program;
    class simple_node_render_instance_t : public wf::scene::transformer_render_instance_t<node_t>
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
            wayfire_view view) : wf::scene::transformer_render_instance_t<node_t>(self,
                push_damage,
                view->get_output())
        {
            this->self = self;
            this->view = view;
            this->push_to_parent = push_damage;
            self->connect(&on_node_damaged);
        }

        ~simple_node_render_instance_t()
        {
        }

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

        void render(const wf::render_target_t& target,
            const wf::region_t& region)
        {
            wlr_box fb_geom =
                target.framebuffer_box_from_geometry_box(target.geometry);
            auto view_box = target.framebuffer_box_from_geometry_box(
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

            OpenGL::render_begin(target);

            /* Upload data to shader */
            auto src_tex = wf::scene::transformer_render_instance_t<node_t>::get_texture(
                1.0);
            this->self->shader->use(src_tex.type);
            this->self->shader->attrib_pointer("position", 2, 0, vertexData);
            this->self->shader->attrib_pointer("texcoord", 2, 0, texCoords);
            this->self->shader->uniformMatrix4f("mvp", target.transform);
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            this->self->shader->set_active_texture(src_tex);

            /* Render it to target */
            target.bind();
            GL_CALL(glViewport(x, fb_geom.height - y - h, w, h));

            GL_CALL(glEnable(GL_BLEND));
            GL_CALL(glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA));

            for (const auto& box : region)
            {
                target.logic_scissor(wlr_box_from_pixman_box(box));
                GL_CALL(glDrawArrays(GL_TRIANGLE_FAN, 0, 4));
            }

            /* Disable stuff */
            GL_CALL(glDisable(GL_BLEND));
            GL_CALL(glActiveTexture(GL_TEXTURE0));
            GL_CALL(glBindTexture(GL_TEXTURE_2D, 0));
            GL_CALL(glBindFramebuffer(GL_FRAMEBUFFER, 0));

            this->self->shader->deactivate();
            OpenGL::render_end();
        }
    };

    wf_filters(wayfire_view view, std::string shader_path) : wf::scene::view_2d_transformer_t(view)
    {
        this->view    = view;
        this->shader = &program;

        std::ifstream t(shader_path);
        std::string fragment_shader((std::istreambuf_iterator<char>(t)), std::istreambuf_iterator<char>());
        OpenGL::render_begin();
        program.compile(vertex_shader, fragment_shader);
        OpenGL::render_end();
    }

    void gen_render_instances(std::vector<render_instance_uptr>& instances,
        damage_callback push_damage, wf::output_t *shown_on) override
    {
        instances.push_back(std::make_unique<simple_node_render_instance_t>(
            this, push_damage, view));
    }

    virtual ~wf_filters()
    {
        OpenGL::render_begin();
        program.free_resources();
        OpenGL::render_end();
    }
};

class wayfire_filters : public wf::plugin_interface_t
{
    OpenGL::program_t program;
    std::map<wayfire_view, std::shared_ptr<wf_filters>> transformers;
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
    }

    std::shared_ptr<wf_filters> ensure_transformer(wayfire_view view, std::string shader_path)
    {
        auto tmgr = view->get_transformed_node();
        if (tmgr->get_transformer<wf::scene::node_t>(transformer_name))
        {
            pop_transformer(view);
        }

        auto node = std::make_shared<wf_filters>(view, shader_path);
        tmgr->add_transformer(node, wf::TRANSFORMER_2D, transformer_name);

        return tmgr->get_transformer<wf_filters>(transformer_name);
    }

    wf::ipc::method_callback ipc_set_view_shader = [=] (nlohmann::json data) -> nlohmann::json
    {
        WFJSON_EXPECT_FIELD(data, "view-id", number_unsigned);
        WFJSON_EXPECT_FIELD(data, "shader-path", string);

        auto view = wf::ipc::find_view_by_id(data["view-id"]);
        if (view && view->is_mapped())
        {
            transformers[view] = ensure_transformer(view, data["shader-path"]);
            if (transformers[view]->program.get_program_id(wf::TEXTURE_TYPE_RGBA) == 0)
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
        return wf::ipc::json_ok();
    };

    wf::ipc::method_callback ipc_unset_view_shader = [=] (nlohmann::json data) -> nlohmann::json
    {
        WFJSON_EXPECT_FIELD(data, "view-id", number_unsigned);

        auto view = wf::ipc::find_view_by_id(data["view-id"]);
        if (view)
        {
            pop_transformer(view);
        }

        return wf::ipc::json_ok();
    };

    void fini() override
    {
        ipc_repo->unregister_method("wf/filters/set-view-shader");
        ipc_repo->unregister_method("wf/filters/unset-view-shader");

        remove_transformers();
    }
};
}
}
}

DECLARE_WAYFIRE_PLUGIN(wf::scene::filters::wayfire_filters);
