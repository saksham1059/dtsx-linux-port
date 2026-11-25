/* Minimal LADSPA 1.1 ABI declarations (public plugin ABI).
 *
 * This header mirrors the ABI of the official LADSPA 1.1 SDK header
 * (ladspa.h, LGPL-2.1-or-later). Only the subset needed by this project is
 * declared, but the layout and scalar types are intentionally identical to the
 * official header: LADSPA_PortDescriptor and LADSPA_PortRangeHintDescriptor
 * are `int`, and the descriptor contains the optional run_adding() function
 * pointer followed by set_run_adding_gain(), deactivate() and cleanup().
 *
 * Getting these two details wrong makes PipeWire's filter-graph read a
 * malformed port map (the host and the plugin disagree on array stride and
 * descriptor field offsets), so the virtual sink is created but the graph
 * refuses to start. Do not change these types without checking the official
 * LADSPA 1.1 header.
 */
#ifndef DTSX_LADSPA_H
#define DTSX_LADSPA_H

#ifdef __cplusplus
extern "C" {
#endif

typedef float LADSPA_Data;
typedef void *LADSPA_Handle;
typedef int LADSPA_Properties;
typedef int LADSPA_PortDescriptor;
typedef int LADSPA_PortRangeHintDescriptor;

#define LADSPA_PROPERTY_REALTIME       0x1
#define LADSPA_PROPERTY_INPLACE_BROKEN 0x2
#define LADSPA_PROPERTY_HARD_RT_CAPABLE 0x4

#define LADSPA_PORT_INPUT   0x1
#define LADSPA_PORT_OUTPUT  0x2
#define LADSPA_PORT_CONTROL 0x4
#define LADSPA_PORT_AUDIO   0x8

#define LADSPA_HINT_BOUNDED_BELOW 0x1
#define LADSPA_HINT_BOUNDED_ABOVE 0x2
#define LADSPA_HINT_TOGGLED       0x4
#define LADSPA_HINT_SAMPLE_RATE   0x8
#define LADSPA_HINT_LOGARITHMIC   0x10
#define LADSPA_HINT_INTEGER       0x20
#define LADSPA_HINT_DEFAULT_MASK  0x3C0
#define LADSPA_HINT_DEFAULT_0     0x40
#define LADSPA_HINT_DEFAULT_1     0x80
#define LADSPA_HINT_DEFAULT_100   0xC0
#define LADSPA_HINT_DEFAULT_MINIMUM 0x180
#define LADSPA_HINT_DEFAULT_LOW   0x1C0
#define LADSPA_HINT_DEFAULT_MIDDLE 0x200
#define LADSPA_HINT_DEFAULT_HIGH  0x240
#define LADSPA_HINT_DEFAULT_MAXIMUM 0x280

typedef struct {
    LADSPA_PortRangeHintDescriptor HintDescriptor;
    LADSPA_Data LowerBound;
    LADSPA_Data UpperBound;
} LADSPA_PortRangeHint;

typedef struct _LADSPA_Descriptor LADSPA_Descriptor;

struct _LADSPA_Descriptor {
    unsigned long UniqueID;
    const char *Label;
    LADSPA_Properties Properties;
    const char *Name;
    const char *Maker;
    const char *Copyright;
    unsigned long PortCount;
    const LADSPA_PortDescriptor *PortDescriptors;
    const char *const *PortNames;
    const LADSPA_PortRangeHint *PortRangeHints;
    void *ImplementationData;
    LADSPA_Handle (*instantiate)(const LADSPA_Descriptor *Descriptor,
                                 unsigned long SampleRate);
    void (*connect_port)(LADSPA_Handle Instance, unsigned long Port,
                         LADSPA_Data *DataLocation);
    void (*activate)(LADSPA_Handle Instance);
    void (*run)(LADSPA_Handle Instance, unsigned long SampleCount);
    void (*run_adding)(LADSPA_Handle Instance, unsigned long SampleCount);
    void (*set_run_adding_gain)(LADSPA_Handle Instance, LADSPA_Data Gain);
    void (*deactivate)(LADSPA_Handle Instance);
    void (*cleanup)(LADSPA_Handle Instance);
};

const LADSPA_Descriptor *ladspa_descriptor(unsigned long Index);

#ifdef __cplusplus
}
#endif
#endif
