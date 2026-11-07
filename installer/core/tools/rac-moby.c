/*
 * rac-moby: moby class cores from one game's format to another's, and a class's mesh as glTF, from the command
 * line. Written from wrench's mobyconvert (the Executive Edition's fork of wrench, src/mobyconvert.cpp) and its
 * unpack of a moby class (src/wrenchbuild/classes/moby_class.cpp), by chaoticgd, GPL-3.0: the same arguments and
 * the same output bytes, and the same mesh.
 *
 *   rac-moby convert IN INGAME OUT OUTGAME [REF] [--rebind FROM TO ZTOP ZBOTTOM] [--mesh GLB]
 *   rac-moby mesh IN GAME OUT.glb [--textures N]
 *
 * Games: rac (R&C1), gc (R&C2). For R&C1 output, header bytes 0xa and 0xb (which only R&C1 uses) are copied from
 * the reference class REF, e.g. the R&C1 class the converted one stands in for; without one they are the input's.
 * --rebind moves the skin of the mesh below a height from joint FROM to joint TO (see moby_rebind in moby.h).
 * --mesh rebuilds the high and low LOD of a class without joints from the glb's node "moby" (see moby_mesh_replace
 * in moby_mesh.h). The output is read back as OUTGAME to make sure it parses.
 *
 * mesh writes the class's mesh as wrench's unpack writes mesh.glb, with materials material_0 .. material_<N - 1>
 * for its N textures (the level's list, which the class core does not hold; by default as many as the packets use).
 */
#include "moby.h"
#include "moby_mesh.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---------------------------------------------------------------------------------------------------- files */

static uint8_t *read_all(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long length;

    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) == 0 && (length = ftell(f)) >= 0 && fseek(f, 0, SEEK_SET) == 0) {
        data = malloc((size_t)length + 1);
        if (data && fread(data, 1, (size_t)length, f) != (size_t)length) {
            free(data);
            data = NULL;
        }
        *size = (size_t)length;
    }
    fclose(f);
    return data;
}

static int write_all(const char *path, const uint8_t *data, size_t size)
{
    FILE *f = fopen(path, "wb");
    int ok = f && fwrite(data, 1, size, f) == size;

    if (f && fclose(f) != 0)
        ok = 0;
    return ok;
}

static int usage(void)
{
    fputs("usage: rac-moby convert IN INGAME OUT OUTGAME [REF] [--rebind FROM TO ZTOP ZBOTTOM] [--mesh GLB]\n"
          "       rac-moby mesh IN GAME OUT.glb [--textures N]\n"
          "games: rac (R&C1), gc (R&C2)\n", stderr);
    return 2;
}

static int parse_game(const char *name, enum moby_game *game)
{
    if (strcmp(name, "rac") == 0) {
        *game = MOBY_RAC;
        return 1;
    }
    if (strcmp(name, "gc") == 0) {
        *game = MOBY_GC;
        return 1;
    }
    fprintf(stderr, "rac-moby: unknown game '%s' (rac, gc)\n", name);
    return 0;
}

static int convert(int argc, char **argv)
{
    const char *in_path = argv[2], *out_path = argv[4], *ref_path, *mesh_path = NULL;
    char **rebind = NULL;
    enum moby_game in_game, out_game;
    uint8_t *in, *out;
    size_t in_size = 0, out_size = 0;
    struct moby_class *moby, *check;
    char error[256];

    if (argc >= 8 && strcmp(argv[argc - 2], "--mesh") == 0) {
        mesh_path = argv[argc - 1];
        argc -= 2;
    }
    if (argc >= 11 && strcmp(argv[argc - 5], "--rebind") == 0) {
        rebind = &argv[argc - 4];
        argc -= 5;
    }
    ref_path = argc == 7 ? argv[6] : NULL;
    if (argc != 6 && argc != 7)
        return usage();
    if (!parse_game(argv[3], &in_game) || !parse_game(argv[5], &out_game))
        return 2;
    in = read_all(in_path, &in_size);
    if (!in) {
        fprintf(stderr, "rac-moby: cannot read %s\n", in_path);
        return 1;
    }
    moby = moby_read(in, in_size, in_game, error, sizeof error);
    if (!moby) {
        fprintf(stderr, "rac-moby: %s: %s\n", in_path, error);
        return 1;
    }

    if (out_game == MOBY_RAC && ref_path) {
        size_t ref_size = 0;
        uint8_t *ref = read_all(ref_path, &ref_size);

        if (!ref || ref_size < 0xc) {
            fprintf(stderr, "rac-moby: cannot read the reference class %s\n", ref_path);
            return 1;
        }
        moby->rac1_byte_a = ref[0xa];
        moby->rac1_byte_b = ref[0xb];
        free(ref);
    }

    if (rebind) {
        int from = atoi(rebind[0]), to = atoi(rebind[1]), z_top = atoi(rebind[2]), z_bottom = atoi(rebind[3]);
        struct moby_rebind_counts counts;

        if (moby_rebind(moby, from, to, z_top, z_bottom, &counts, error, sizeof error) != 0) {
            fprintf(stderr, "rac-moby: --rebind: %s\n", error);
            return 1;
        }
        printf("rebind joint %d to %d below z %d..%d: %d high LOD, %d low LOD and %d metal vertices changed\n",
               from, to, z_top, z_bottom, counts.high, counts.low, counts.metal);
    }

    if (mesh_path) {
        size_t glb_size = 0;
        uint8_t *glb = read_all(mesh_path, &glb_size);
        struct moby_mesh_counts counts;

        if (!glb) {
            fprintf(stderr, "rac-moby: cannot read %s\n", mesh_path);
            return 1;
        }
        if (moby_mesh_replace(moby, glb, glb_size, &counts, error, sizeof error) != 0) {
            fprintf(stderr, "rac-moby: --mesh %s: %s\n", mesh_path, error);
            return 1;
        }
        free(glb);
        printf("mesh %s: %zu vertices, %zu primitives -> %zu packets per LOD\n", mesh_path, counts.vertices,
               counts.primitives, counts.packets);
    }

    out = moby_write(moby, out_game, &out_size, error, sizeof error);
    if (!out) {
        fprintf(stderr, "rac-moby: %s\n", error);
        return 1;
    }
    if (!write_all(out_path, out, out_size)) {
        fprintf(stderr, "rac-moby: cannot write %s\n", out_path);
        return 1;
    }

    check = moby_read(out, out_size, out_game, error, sizeof error);
    if (!check) {
        fprintf(stderr, "rac-moby: the output does not read back: %s\n", error);
        return 1;
    }
    printf("%s: %zu bytes, %zu high, %zu low, %zu metal packets, %zu sequences, %d joints\n", out_path, out_size,
           check->high_count, check->low_count, check->metal_count, check->sequence_count, check->joint_count);
    moby_free(check);
    moby_free(moby);
    free(out);
    free(in);
    return 0;
}

static int mesh(int argc, char **argv)
{
    const char *in_path = argv[2], *out_path = argv[4];
    enum moby_game game;
    uint8_t *in, *glb;
    size_t in_size = 0, glb_size = 0;
    struct moby_class *moby;
    int texture_count = -1;
    char error[256];

    if (argc == 7 && strcmp(argv[5], "--textures") == 0)
        texture_count = atoi(argv[6]);
    else if (argc != 5)
        return usage();
    if (!parse_game(argv[3], &game))
        return 2;
    in = read_all(in_path, &in_size);
    if (!in) {
        fprintf(stderr, "rac-moby: cannot read %s\n", in_path);
        return 1;
    }
    moby = moby_read(in, in_size, game, error, sizeof error);
    if (!moby) {
        fprintf(stderr, "rac-moby: %s: %s\n", in_path, error);
        return 1;
    }
    glb = moby_mesh_export(moby, texture_count, &glb_size, error, sizeof error);
    if (!glb) {
        fprintf(stderr, "rac-moby: %s: %s\n", in_path, error);
        return 1;
    }
    if (!write_all(out_path, glb, glb_size)) {
        fprintf(stderr, "rac-moby: cannot write %s\n", out_path);
        return 1;
    }
    printf("%s: %zu bytes\n", out_path, glb_size);
    free(glb);
    moby_free(moby);
    free(in);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "convert") == 0)
        return convert(argc, argv);
    if (argc >= 2 && strcmp(argv[1], "mesh") == 0)
        return mesh(argc, argv);
    return usage();
}
