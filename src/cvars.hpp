#include "string_utils.hpp"
#include <cstdint>

class CVarParameter;

class CVarSystem
{
public:
    static CVarSystem *get();

    virtual CVarParameter *get_cvar(StringUtils::StringHash hash) = 0;

    virtual double *get_float_cvar(StringUtils::StringHash hash) = 0;

    virtual int32_t *get_int_cvar(StringUtils::StringHash hash) = 0;

    virtual const char *get_string_cvar(StringUtils::StringHash hash) = 0;

    virtual void set_float_cvar(StringUtils::StringHash hash, double value) = 0;

    virtual void set_int_cvar(StringUtils::StringHash hash, int32_t value) = 0;

    virtual void set_string_cvar(StringUtils::StringHash hash, const char *value) = 0;

    virtual CVarParameter *create_float_cvar(
        const char *name, const char *description, double default_value, double current_value) = 0;

    virtual CVarParameter *create_int_cvar(const char *name, const char *description,
        int32_t default_value, int32_t current_value) = 0;

    virtual CVarParameter *create_string_cvar(const char *name, const char *description,
        const char *default_value, const char *current_value) = 0;

    virtual void draw_imgui_editor() = 0;
};

enum class CVarFlags : uint32_t
{
    None = 0,
    Noedit = 1 << 1,
    EditReadOnly = 1 << 2,
    Advanced = 1 << 3,

    EditCheckbox = 1 << 8,
    EditFloatDrag = 1 << 9,
};

template <typename T>
struct AutoCVar
{
protected:
    int index;
    using CVarType = T;
};

struct AutoCVar_Float : AutoCVar<double>
{
    AutoCVar_Float(const char *name, const char *description, double default_vlaue,
        CVarFlags flags = CVarFlags::None);

    double get();

    void set(double val);
};

struct AutoCVar_Int : AutoCVar<int32_t>
{
    AutoCVar_Int(const char *name, const char *description, int32_t default_value,
        CVarFlags flags = CVarFlags::None);

    int32_t get();

    void set(int32_t val);
};

struct AutoCVar_String : AutoCVar<std::string>
{
    AutoCVar_String(const char *name, const char *description, const char *default_value,
        CVarFlags flags = CVarFlags::None);

    const char *get();

    void set(const std::string val);
};