// Scrap is a project that allows anyone to build software using simple, block based interface.
//
// Copyright (C) 2024-2025 Grisshink
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#define SCRVM_IMPLEMENTATION
#include "term.h"
#include "scrap.h"
#include "blocks.h"

#include <math.h>

#define CONVERT_COLOR(color, type) (type) { color.r, color.g, color.b, color.a }

Image logo_img;

Shader line_shader;
int shader_time_loc;

ScrExec exec = {0};

Vector2 camera_pos = {0};
Vector2 camera_click_pos = {0};

Config conf;
HoverInfo hover_info = {0};
Shader line_shader;

Font font_cond;
Font font_cond_shadow;
Font font_eb;
Font font_mono;

Texture2D run_tex;
Texture2D stop_tex;
Texture2D drop_tex;
Texture2D close_tex;
Texture2D logo_tex;
Texture2D warn_tex;
Texture2D edit_tex;
Texture2D close_tex;
Texture2D term_tex;
Texture2D add_arg_tex;
Texture2D del_arg_tex;
Texture2D add_text_tex;
Texture2D special_tex;
Texture2D list_tex;

TabType current_tab = TAB_CODE;
ScrVm vm;
Vector2 camera_pos;
ActionBar actionbar;
BlockCode block_code = {0};
Dropdown dropdown = {0};
Sidebar sidebar = {0};
ScrBlockChain* editor_code = {0};
DrawStack* draw_stack = NULL;
ScrBlockChain mouse_blockchain = {0};
Gui* gui = NULL;

float shader_time = 0.0;
int blockchain_select_counter = -1;

const char* line_shader_vertex =
    "#version 330\n"
    "in vec3 vertexPosition;\n"
    "in vec4 vertexColor;\n"
    "out vec2 fragCoord;\n"
    "out vec4 fragColor;\n"
    "uniform mat4 mvp;\n"
    "void main() {\n"
    "    vec4 pos = mvp * vec4(vertexPosition, 1.0);\n"
    "    fragCoord = pos.xy;\n"
    "    fragColor = vertexColor;\n"
    "    gl_Position = pos;\n"
    "}";

const char* line_shader_fragment =
    "#version 330\n"
    "in vec2 fragCoord;\n"
    "in vec4 fragColor;\n"
    "out vec4 finalColor;\n"
    "uniform float time = 0.0;\n"
    "void main() {\n"
    "    vec2 coord = (fragCoord + 1.0) * 0.5;\n"
    "    coord.y = 1.0 - coord.y;\n"
    "    float pos = time * 4.0 - 1.0;\n"
    "    float diff = clamp(1.0 - abs(coord.x + coord.y - pos), 0.0, 1.0);\n"
    "    finalColor = vec4(fragColor.xyz, pow(diff, 2.0));\n"
    "}";

char* top_bar_buttons_text[3] = {
    "File",
    "Settings",
    "About",
};

char* tab_bar_buttons_text[2] = {
    "Code",
    "Output",
};

void blockcode_add_blockchain(BlockCode* blockcode, ScrBlockChain chain) {
    vector_add(&editor_code, chain);
    blockcode_update_measurments(blockcode);
}

void blockcode_remove_blockchain(BlockCode* blockcode, size_t ind) {
    vector_remove(editor_code, ind);
    blockcode_update_measurments(blockcode);
}

void sanitize_block(ScrBlock* block) {
    for (vec_size_t i = 0; i < vector_size(block->arguments); i++) {
        if (block->arguments[i].type != ARGUMENT_BLOCK) continue;
        if (block->arguments[i].data.block.parent != block) {
            TraceLog(LOG_ERROR, "Block %p detached from parent %p! (Got %p)", &block->arguments[i].data.block, block, block->arguments[i].data.block.parent);
            assert(false);
            return;
        }
        sanitize_block(&block->arguments[i].data.block);
    }
}

void sanitize_links(void) {
    for (vec_size_t i = 0; i < vector_size(editor_code); i++) {
        ScrBlock* blocks = editor_code[i].blocks;
        for (vec_size_t j = 0; j < vector_size(blocks); j++) {
            sanitize_block(&blocks[j]);
        }
    }

    for (vec_size_t i = 0; i < vector_size(mouse_blockchain.blocks); i++) {
        sanitize_block(&mouse_blockchain.blocks[i]);
    }
}

Texture2D load_svg(const char* path) {
    Image svg_img = LoadImageSvg(path, conf.font_size, conf.font_size);
    Texture2D texture = LoadTextureFromImage(svg_img);
    SetTextureFilter(texture, TEXTURE_FILTER_BILINEAR);
    UnloadImage(svg_img);
    return texture;
}

const char* get_font_path(char* font_path) {
    return font_path[0] != '/' && font_path[1] != ':' ? into_data_path(font_path) : font_path;
}

GuiMeasurement scrap_gui_measure_image(void* image, int size) {
    Texture2D* img = image;
    return (GuiMeasurement) { img->width * ((float)size / (float)img->height), size };
}

GuiMeasurement scrap_gui_measure_text(const char* text, int size) {
    Vector2 text_size = MeasureTextEx(font_cond_shadow, text, size, 0.0);
    return (GuiMeasurement) { text_size.x, text_size.y };
}

GuiColor as_gui_color(Color color) {
    return (GuiColor) { color.r, color.g, color.b, color.a };
}

Color as_gui_rl_color(GuiColor color) {
    return (Color) { color.r, color.g, color.b, color.a };
}

void scrap_gui_draw_blockdef(ScrBlockdef* blockdef) {
    bool collision = false;
    Color block_color = CONVERT_COLOR(blockdef->color, Color);
    Color outline_color = ColorBrightness(block_color, collision ? 0.5 : -0.2);

    gui_layout_begin_static(gui, BLOCK_OUTLINE_SIZE * 2, BLOCK_OUTLINE_SIZE * 2, CONVERT_COLOR(block_color, GuiColor), CONVERT_COLOR(outline_color, GuiColor), BLOCK_OUTLINE_SIZE);
    gui_layout_begin_horizontal(gui, BLOCK_PADDING, ALIGN_CENTER, NO_COLOR);
    gui_layout_set_min_size(gui, 0, conf.font_size - BLOCK_OUTLINE_SIZE * 4);

    for (size_t i = 0; i < vector_size(blockdef->inputs); i++) {
        ScrInput* input = &blockdef->inputs[i];

        switch (input->type) {
        case INPUT_TEXT_DISPLAY:
            gui_draw_text(gui, input->data.stext.text, BLOCK_TEXT_SIZE, CONVERT_COLOR(WHITE, GuiColor));
            break;
        case INPUT_IMAGE_DISPLAY:
            gui_draw_image(gui, input->data.simage.image.image_ptr, BLOCK_IMAGE_SIZE, CONVERT_COLOR(WHITE, GuiColor));
            break;
        case INPUT_ARGUMENT:
            scrap_gui_draw_blockdef(input->data.arg.blockdef);
            break;
        default:
            gui_draw_text(gui, "NODEF", BLOCK_TEXT_SIZE, as_gui_color(RED));
            break;
        }
    }
    gui_layout_end_horizontal(gui);
    gui_layout_end_static(gui);
}

void scrap_gui_draw_block(ScrBlock* block) {
    bool collision = false;
    Color block_color = CONVERT_COLOR(block->blockdef->color, Color);
    Color dropdown_color = ColorBrightness(block_color, collision ? 0.0 : -0.3);
    Color outline_color = ColorBrightness(block_color, collision ? 0.5 : -0.2);

    gui_layout_begin_static(gui, BLOCK_OUTLINE_SIZE * 2, BLOCK_OUTLINE_SIZE * 2, CONVERT_COLOR(block_color, GuiColor), CONVERT_COLOR(outline_color, GuiColor), BLOCK_OUTLINE_SIZE);
    gui_layout_begin_horizontal(gui, BLOCK_PADDING, ALIGN_CENTER, NO_COLOR);
    gui_layout_set_min_size(gui, 0, conf.font_size - BLOCK_OUTLINE_SIZE * 4);

    size_t arg_id = 0;
    for (size_t i = 0; i < vector_size(block->blockdef->inputs); i++) {
        ScrInput* input = &block->blockdef->inputs[i];
        ScrArgument* arg = &block->arguments[arg_id];

        switch (input->type) {
        case INPUT_TEXT_DISPLAY:
            gui_draw_text(gui, input->data.stext.text, BLOCK_TEXT_SIZE, CONVERT_COLOR(WHITE, GuiColor));
            break;
        case INPUT_IMAGE_DISPLAY:
            gui_draw_image(gui, input->data.simage.image.image_ptr, BLOCK_IMAGE_SIZE, CONVERT_COLOR(WHITE, GuiColor));
            break;
        case INPUT_ARGUMENT:
            switch (arg->type) {
            case ARGUMENT_TEXT:
            case ARGUMENT_CONST_STRING:
                gui_layout_begin_static(gui, BLOCK_STRING_PADDING / 2, 0, CONVERT_COLOR(WHITE, GuiColor), NO_BORDER);
                    gui_layout_begin_horizontal(gui, 0, ALIGN_CENTER, NO_COLOR);
                        gui_layout_set_min_size(gui, 0, conf.font_size - BLOCK_OUTLINE_SIZE * 4);
                        gui_draw_text(gui, arg->data.text, BLOCK_TEXT_SIZE, CONVERT_COLOR(BLACK, GuiColor));
                    gui_layout_end_horizontal(gui);
                gui_layout_end_static(gui);
                break;
            case ARGUMENT_BLOCK:
                scrap_gui_draw_block(&arg->data.block);
                break;
            default:
                break;
            }
            arg_id++;
            break;
        case INPUT_DROPDOWN:
            assert(arg->type == ARGUMENT_CONST_STRING);
            gui_layout_begin_static(gui, BLOCK_STRING_PADDING / 2, 0, CONVERT_COLOR(dropdown_color, GuiColor), NO_BORDER);
                gui_layout_begin_horizontal(gui, 0, ALIGN_CENTER, NO_COLOR);
                    gui_layout_set_min_size(gui, 0, conf.font_size - BLOCK_OUTLINE_SIZE * 4);
                    gui_draw_text(gui, arg->data.text, BLOCK_TEXT_SIZE, CONVERT_COLOR(WHITE, GuiColor));
                    gui_draw_image(gui, &drop_tex, BLOCK_IMAGE_SIZE, CONVERT_COLOR(WHITE, GuiColor));
                gui_layout_end_horizontal(gui);
            gui_layout_end_static(gui);
            arg_id++;
            break;
        case INPUT_BLOCKDEF_EDITOR:
            assert(arg->type == ARGUMENT_BLOCKDEF);
            gui_layout_begin_static(gui, BLOCK_OUTLINE_SIZE * 2, BLOCK_OUTLINE_SIZE, CONVERT_COLOR(dropdown_color, GuiColor), NO_BORDER);
                gui_layout_begin_horizontal(gui, BLOCK_PADDING, ALIGN_CENTER, NO_COLOR);
                    scrap_gui_draw_blockdef(arg->data.blockdef);
                    gui_layout_begin_static(gui, 0, 0, (GuiColor) { 0xff, 0xff, 0xff, 0x40 }, NO_BORDER);
                        gui_draw_image(gui, &edit_tex, BLOCK_IMAGE_SIZE, CONVERT_COLOR(WHITE, GuiColor));
                    gui_layout_end_static(gui);
                    gui_layout_begin_static(gui, 0, 0, (GuiColor) { 0xff, 0xff, 0xff, 0x40 }, NO_BORDER);
                        gui_draw_image(gui, &close_tex, BLOCK_IMAGE_SIZE, CONVERT_COLOR(WHITE, GuiColor));
                    gui_layout_end_static(gui);
                gui_layout_end_horizontal(gui);
            gui_layout_end_static(gui);
            arg_id++;
            break;
        default:
            gui_draw_text(gui, "NODEF", BLOCK_TEXT_SIZE, as_gui_color(RED));
            break;
        }
    }
    gui_layout_end_horizontal(gui);
    gui_layout_end_static(gui);
}

void scrap_gui_process(void) {
    // Gui
    gui_begin(gui, conf.side_bar_size + 10, conf.font_size * 2.2 + 10);
        gui_layout_begin_vertical(gui, 5, ALIGN_LEFT, NO_COLOR);
            for (size_t i = dropdown.scroll_amount; i < vector_size(sidebar.blocks); i++) {
                scrap_gui_draw_block(&sidebar.blocks[i]);
            }
        gui_layout_end_vertical(gui);
    gui_end(gui);
}

void scrap_gui_render(void) {
    DrawCommand* command;
    GUI_GET_COMMANDS(gui, command) {
        Texture2D* image = command->data.image;

        switch (command->type) {
        case DRAWTYPE_UNKNOWN:
            assert(false && "Got unknown draw type");
            break;
        case DRAWTYPE_BORDER:
            DrawRectangleLinesEx((Rectangle) { command->pos_x, command->pos_y, command->width, command->height }, command->data.border_width, as_gui_rl_color(command->color));
            break;
        case DRAWTYPE_RECT:
            DrawRectangle(command->pos_x, command->pos_y, command->width, command->height, as_gui_rl_color(command->color));
            break;
        case DRAWTYPE_TEXT:
            DrawTextEx(font_cond_shadow, command->data.text, (Vector2) { command->pos_x, command->pos_y }, command->height, 0.0, as_gui_rl_color(command->color));
            break;
        case DRAWTYPE_IMAGE:
            DrawTextureEx(
                *image, 
                (Vector2) { command->pos_x + SHADOW_DISTANCE, command->pos_y + SHADOW_DISTANCE },
                0.0,
                (float)command->height / (float)image->height,
                (Color) { 0x00, 0x00, 0x00, 0x80 }
            );
            DrawTextureEx(
                *image, 
                (Vector2) { command->pos_x, command->pos_y},
                0.0,
                (float)command->height / (float)image->height,
                as_gui_rl_color(command->color)
            );
            break;
        default:
            assert(false && "Unimplemented command render");
            break;
        }
#ifdef DEBUG
        DrawRectangleLinesEx((Rectangle) { command->pos_x, command->pos_y, command->width, command->height }, 1.0, PINK);
#endif
    }
}

void setup(void) {
    run_tex = LoadTexture(into_data_path(DATA_PATH "run.png"));
    SetTextureFilter(run_tex, TEXTURE_FILTER_BILINEAR);
    drop_tex = LoadTexture(into_data_path(DATA_PATH "drop.png"));
    SetTextureFilter(drop_tex, TEXTURE_FILTER_BILINEAR);
    close_tex = LoadTexture(into_data_path(DATA_PATH "close.png"));
    SetTextureFilter(close_tex, TEXTURE_FILTER_BILINEAR);

    logo_img = LoadImageSvg(into_data_path(DATA_PATH "logo.svg"), conf.font_size, conf.font_size);
    logo_tex = LoadTextureFromImage(logo_img);
    SetTextureFilter(logo_tex, TEXTURE_FILTER_BILINEAR);

    warn_tex = load_svg(into_data_path(DATA_PATH "warning.svg"));
    stop_tex = load_svg(into_data_path(DATA_PATH "stop.svg"));
    edit_tex = load_svg(into_data_path(DATA_PATH "edit.svg"));
    close_tex = load_svg(into_data_path(DATA_PATH "close.svg"));
    term_tex = load_svg(into_data_path(DATA_PATH "term.svg"));
    add_arg_tex = load_svg(into_data_path(DATA_PATH "add_arg.svg"));
    del_arg_tex = load_svg(into_data_path(DATA_PATH "del_arg.svg"));
    add_text_tex = load_svg(into_data_path(DATA_PATH "add_text.svg"));
    special_tex = load_svg(into_data_path(DATA_PATH "special.svg"));
    list_tex = load_svg(into_data_path(DATA_PATH "list.svg"));

    int codepoints_count;
    int *codepoints = LoadCodepoints(conf.font_symbols, &codepoints_count);
    font_cond = LoadFontEx(get_font_path(conf.font_path), conf.font_size, codepoints, codepoints_count);
    font_cond_shadow = LoadFontEx(get_font_path(conf.font_path), BLOCK_TEXT_SIZE, codepoints, codepoints_count);
    font_eb = LoadFontEx(get_font_path(conf.font_bold_path), conf.font_size, codepoints, codepoints_count);
    font_mono = LoadFontEx(get_font_path(conf.font_mono_path), conf.font_size, codepoints, codepoints_count);
    UnloadCodepoints(codepoints);

    SetTextureFilter(font_cond.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(font_cond_shadow.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(font_eb.texture, TEXTURE_FILTER_BILINEAR);
    SetTextureFilter(font_mono.texture, TEXTURE_FILTER_BILINEAR);

    prerender_font_shadow(&font_cond_shadow);

    line_shader = LoadShaderFromMemory(line_shader_vertex, line_shader_fragment);
    shader_time_loc = GetShaderLocation(line_shader, "time");

    vm = vm_new();
    load_blocks(&vm);

    mouse_blockchain = blockchain_new();
    draw_stack = vector_create();
    editor_code = vector_create();

    sidebar_init();

    term_init();

    init_gui();

    gui = malloc(sizeof(Gui));
    gui_init(gui);
    gui_set_measure_text_func(gui, scrap_gui_measure_text);
    gui_set_measure_image_func(gui, scrap_gui_measure_image);
    gui_update_window_size(gui, GetScreenWidth(), GetScreenHeight());
    TraceLog(LOG_INFO, "Allocated %.2f KiB for gui", (float)sizeof(Gui) / 1024.0f);
}

int main(void) {
    set_default_config(&conf);
    load_config(&conf);

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
    InitWindow(800, 600, "Scrap");
    SetWindowState(FLAG_VSYNC_HINT);
    SetTargetFPS(conf.fps_limit);

    setup();
    SetWindowIcon(logo_img);

    while (!WindowShouldClose()) {
        process_input();

        actionbar.show_time -= GetFrameTime();
        if (actionbar.show_time < 0) actionbar.show_time = 0;

        if (shader_time_loc != -1) SetShaderValue(line_shader, shader_time_loc, &shader_time, SHADER_UNIFORM_FLOAT);
        shader_time += GetFrameTime() / 2.0;
        if (shader_time >= 1.0) shader_time = 1.0;

        // I have no idea why, but this code may occasionally crash X server, so it is turned off for now
        /*if (hover_info.argument || hover_info.select_argument) {
            SetMouseCursor(MOUSE_CURSOR_IBEAM);
        } else if (hover_info.block) {
            SetMouseCursor(MOUSE_CURSOR_POINTING_HAND);
        } else {
            SetMouseCursor(MOUSE_CURSOR_DEFAULT);
        }*/

        size_t vm_return = -1;
        if (exec_try_join(&vm, &exec, &vm_return)) {
            if (vm_return == 1) {
                actionbar_show("Vm executed successfully");
            } else if (vm_return == (size_t)PTHREAD_CANCELED) {
                actionbar_show("Vm stopped >:(");
            } else {
                actionbar_show("Vm shitted and died :(");
            }
            exec_free(&exec);
        } else if (vm.is_running) {
            hover_info.exec_chain = exec.running_chain;
            hover_info.exec_ind = exec.chain_stack[exec.chain_stack_len - 1].running_ind;
        }

        BeginDrawing();
        process_render();
        EndDrawing();
    }

    if (vm.is_running) {
        exec_stop(&vm, &exec);
        size_t bin;
        exec_join(&vm, &exec, &bin);
        exec_free(&exec);
    }
    term_free();
    vector_free(draw_stack);
    blockchain_free(&mouse_blockchain);
    for (vec_size_t i = 0; i < vector_size(editor_code); i++) {
        blockchain_free(&editor_code[i]);
    }
    vector_free(editor_code);
    gui_free();
    for (vec_size_t i = 0; i < vector_size(sidebar.blocks); i++) {
        block_free(&sidebar.blocks[i]);
    }
    vector_free(sidebar.blocks);
    vm_free(&vm);
    free(gui);
    CloseWindow();

    return 0;
}
