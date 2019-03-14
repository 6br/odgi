#include "subcommand.hpp"
#include "graph.hpp"
#include "args.hxx"
#include "threads.hpp"

#include "lodepng.h"
#include <iostream>

namespace odgi {

namespace png {

/*
3 ways to encode a PNG from RGBA pixel data to a file (and 2 in-memory ways).
NOTE: this samples overwrite the file or test.png without warning!
*/

//g++ lodepng.cpp examples/example_encode.cpp -I./ -ansi -pedantic -Wall -Wextra -O3

//Example 1
//Encode from raw pixels to disk with a single function call
//The image argument has width * height RGBA pixels or width * height * 4 bytes
void encodeOneStep(const char* filename, std::vector<unsigned char>& image, unsigned width, unsigned height) {
  //Encode the image
  unsigned error = lodepng::encode(filename, image, width, height);

  //if there's an error, display it
  if(error) std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
}

//Example 2
//Encode from raw pixels to an in-memory PNG file first, then write it to disk
//The image argument has width * height RGBA pixels or width * height * 4 bytes
void encodeTwoSteps(const char* filename, std::vector<unsigned char>& image, unsigned width, unsigned height) {
  std::vector<unsigned char> png;

  unsigned error = lodepng::encode(png, image, width, height);
  if(!error) lodepng::save_file(png, filename);

  //if there's an error, display it
  if(error) std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
}

//Example 3
//Save a PNG file to disk using a State, normally needed for more advanced usage.
//The image argument has width * height RGBA pixels or width * height * 4 bytes
void encodeWithState(const char* filename, std::vector<unsigned char>& image, unsigned width, unsigned height) {
  std::vector<unsigned char> png;
  lodepng::State state; //optionally customize this one

  unsigned error = lodepng::encode(png, image, width, height, state);
  if(!error) lodepng::save_file(png, filename);

  //if there's an error, display it
  if(error) std::cout << "encoder error " << error << ": "<< lodepng_error_text(error) << std::endl;
}

}


using namespace odgi::subcommand;

int main_viz(int argc, char** argv) {

    // trick argumentparser to do the right thing with the subcommand
    for (uint64_t i = 1; i < argc-1; ++i) {
        argv[i] = argv[i+1];
    }
    std::string prog_name = "odgi viz";
    argv[0] = (char*)prog_name.c_str();
    --argc;
    
    args::ArgumentParser parser("variation graph visualizations");
    args::HelpFlag help(parser, "help", "display this help summary", {'h', "help"});
    args::ValueFlag<std::string> dg_in_file(parser, "FILE", "load the index from this file", {'i', "idx"});
    args::ValueFlag<std::string> png_out_file(parser, "FILE", "write the output (png) to this file", {'o', "out"});
    args::ValueFlag<uint64_t> image_width(parser, "N", "width in pixels of output image", {'x', "width"});
    args::ValueFlag<uint64_t> image_height(parser, "N", "height in pixels of output image", {'y', "height"});
    args::ValueFlag<float> alpha(parser, "FLOAT", "use this alpha for in aggregation", {'a', "alpha"});
    args::ValueFlag<uint64_t> threads(parser, "N", "number of threads to use", {'t', "threads"});

    try {
        parser.ParseCLI(argc, argv);
    } catch (args::Help) {
        std::cout << parser;
        return 0;
    } catch (args::ParseError e) {
        std::cerr << e.what() << std::endl;
        std::cerr << parser;
        return 1;
    }
    if (argc==1) {
        std::cout << parser;
        return 1;
    }

    if (args::get(threads)) {
        omp_set_num_threads(args::get(threads));
    } else {
        omp_set_num_threads(1);
    }

    graph_t graph;
    assert(argc > 0);
    std::string infile = args::get(dg_in_file);
    if (infile.size()) {
        ifstream f(infile.c_str());
        graph.load(f);
        f.close();
    }

    //NOTE: this sample will overwrite the file or test.png without warning!
    //const char* filename = argc > 1 ? argv[1] : "test.png";
    if (!args::get(png_out_file).size()) {
        std::cerr << "[odgi viz] error: output image required" << std::endl;
        return 1;
    }
    const char* filename = args::get(png_out_file).c_str();

    hash_map<uint64_t, uint64_t> position_map;
    std::vector<std::pair<uint64_t, uint64_t>> contacts;
    uint64_t len = 0;
    graph.for_each_handle([&](const handle_t& h) {
            position_map[number_bool_packing::unpack_number(h)] = len;
            uint64_t hl = graph.get_length(h);
            len += hl;
        });

    uint64_t width = (args::get(image_width) ? args::get(image_width) : 1000);
    uint64_t height = (args::get(image_height) ? args::get(image_height) : 1000);
    std::vector<uint8_t> image;
    image.resize(width * height * 4, 255);
    float scale = (float)width/(float)len;

    float alpha_value = 255*(args::get(alpha) ? args::get(alpha) : 1);

    auto add_point = [&](const uint64_t& _x, const uint64_t& _y) {
        uint64_t x = std::min((uint64_t)std::round(_x * scale), width-1);
        uint64_t y = std::min((uint64_t)std::round(_y * scale), height-1);
        uint8_t v = alpha_value;
        uint8_t* r = &image[4 * width * y + 4 * x + 0];
        uint8_t* g = &image[4 * width * y + 4 * x + 1];
        uint8_t* b = &image[4 * width * y + 4 * x + 2];
        uint8_t* a = &image[4 * width * y + 4 * x + 3];
        if (*r >= v) *r -= v;
        if (*g >= v) *g -= v;
        if (*b >= v) *b -= v;
        *a = 255;
    };

    auto add_edge = [&](const handle_t& h, const handle_t& o) {
        auto& _a = position_map[number_bool_packing::unpack_number(h)];
        auto& _b = position_map[number_bool_packing::unpack_number(o)];
        uint64_t a = std::min(_a, _b);
        uint64_t b = std::max(_a, _b);
        uint64_t dist = b - a;
        uint64_t i = 0;
        for ( ; i < dist; i+=1/scale) {
            add_point(a, i);
        }
        while (a < b) {
            add_point(a, i);
            a += 1/scale;
        }
        for (uint64_t j = 0; j < dist; j+=1/scale) {
            add_point(b, j);
        }
    };

    graph.for_each_handle([&](const handle_t& h) {
            uint64_t p = position_map[number_bool_packing::unpack_number(h)];
            uint64_t hl = graph.get_length(h);
            // make contects for the bases in the node
            for (uint64_t i = 0; i < hl; ++i) {
                add_point(p+i, 0);
            }
        });

    std::cerr << std::endl;
    uint64_t seen = 0;
    graph.for_each_handle([&](const handle_t& h) {
            if (++seen%100==0) std::cerr << "adding edges " << seen << "\r";
            // add contacts for the edges
            graph.follow_edges(h, false, [&](const handle_t& o) {
                    add_edge(h, o);
                });
            /*
            graph.follow_edges(h, true, [&](const handle_t& o) {
                    add_edge(o, h);
                });
            */
        });

    /*
    float path_alpha_value = 1-(args::get(path_alpha) ? args::get(path_alpha) : 1);
    
    graph.for_each_path_handle([&](const path_handle_t& path) {
            uint32_t path_name_hash = djb2_hash32(graph.get_path_name(path).c_str());
            uint8_t path_r = 0;
            uint8_t path_b = 0;
            uint8_t path_g = 0;
            memcpy(&path_r, &path_name_hash, sizeof(uint8_t));
            memcpy(&path_b, ((uint8_t*)&path_name_hash)+1, sizeof(uint8_t));
            memcpy(&path_g, ((uint8_t*)&path_name_hash)+2, sizeof(uint8_t));
            float path_r_f = (float)path_r/(float)255;
            float path_g_f = (float)path_g/(float)255;
            float path_b_f = (float)path_b/(float)255;
            float sum = path_r_f + path_g_f + path_b_f;
            path_r_f /= sum;
            path_g_f /= sum;
            path_b_f /= sum;
            std::cerr << "path " << as_integer(path) << " " << graph.get_path_name(path) << " " << path_r_f << " " << path_g_f << " " << path_b_f << std::endl;
            /// Loop over all the occurrences along a path, from first through last
            //uint64_t x=0, y=0;
            uint64_t step = 0;
            graph.for_each_occurrence_in_path(path, [&](const occurrence_handle_t& occ) {
                    //std::cerr << "occ " << ++step << "\r";
                    handle_t h = graph.get_occurrence(occ);
                    uint64_t p = position_map[number_bool_packing::unpack_number(h)];
                    // todo...
                    uint64_t hl = graph.get_length(h);
                    // make contects for the bases in the node
                    for (uint64_t i = 0; i < hl; ++i) {
                        add_point(p+i, 0, path_r, path_g, path_b, alpha_value, as_integer(path)+1);
                        //add_contact(p+i, p+i+1, 1, 0, 0, path_alpha_value);
                    }
                    //x = p+hl;
                    //y = p+hl;
                 });
         });
    */
    
    png::encodeOneStep(filename, image, width, height);

    return 0;
}

static Subcommand odgi_viz("viz", "visualize the graph",
                           PIPELINE, 3, main_viz);


}