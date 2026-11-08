#include "spu.h"

#include <stdio.h>
#include <string.h>

#include <sys/spu.h>

static bool spus_started, spus_failed;

static bool start_spus(void)
{
    if (!spus_started && !spus_failed) {
        spus_started = sysSpuInitialize(SPU_THREADS, 0) == 0;
        spus_failed = !spus_started;
        if (spus_failed) {
            printf("spu: the SPUs could not be started; the PPU does their work\n");
        }
    }
    return spus_started;
}

static bool import(SpuProgram *program)
{
    if (!program->imported && !program->failed) {
        program->imported = sysSpuImageImport(&program->image, program->elf, 0) == 0;
        program->failed = !program->imported;
        if (program->failed) {
            printf("spu: %s could not be loaded; the PPU does its work\n", program->name);
        }
    }
    return program->imported;
}

bool spu_run(SpuProgram *program, uint32_t threads, void *job)
{
    if (threads == 0 || threads > SPU_THREADS || !start_spus() || !import(program)) {
        return false;
    }
    sysSpuThreadGroupAttribute group_attributes;
    memset(&group_attributes, 0, sizeof group_attributes);
    group_attributes.name = program->name;
    group_attributes.nsize = (u32)strlen(program->name) + 1;
    group_attributes.type = SPU_THREAD_GROUP_TYPE_NORMAL;
    sys_spu_group_t group;
    if (sysSpuThreadGroupCreate(&group, threads, 100, &group_attributes) != 0) {
        return false;
    }
    bool started = true;
    for (u32 i = 0; i < threads && started; i++) {
        sysSpuThreadAttribute attributes = {program->name, (u32)strlen(program->name) + 1, SPU_THREAD_ATTR_NONE};
        sysSpuThreadArgument arguments = {i, (u64)(uintptr_t)job, 0, 0};
        sys_spu_thread_t thread;
        started = sysSpuThreadInitialize(&thread, group, i, &program->image, &attributes, &arguments) == 0;
    }
    u32 cause = 0, status = 0;
    bool ran = started && sysSpuThreadGroupStart(group) == 0 && sysSpuThreadGroupJoin(group, &cause, &status) == 0;
    sysSpuThreadGroupDestroy(group);
    return ran && cause == SPU_THREAD_GROUP_JOIN_ALL_THREADS_EXIT;
}
