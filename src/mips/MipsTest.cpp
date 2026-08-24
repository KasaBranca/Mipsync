#include "MipsTest.h"
#include "MipsRuntime.h"
#include "VM.h"
#include "../core/Log.h"
#include <vector>

namespace MipsyncEngine::Mips {

void RunMipsPhase1Tests() {
    std::vector<std::string> errors;
    if (auto module = MipsRuntime::CompileScriptFile("Rotator.mips", errors)) {
        MIPSYNC_INFO("[Mips#] Rotator OK ({} method(s), {} field(s))",
                      module->methods.size(), module->fields.size());
    } else {
        for (const auto& err : errors)
            MIPSYNC_WARN("[Mips#] {}", err);
    }
}

bool RunMipsRuntimeRegressionTests(const char* scriptPath,
                                   std::vector<std::string>& errors) {
    auto module = MipsRuntime::CompileScriptFile(scriptPath ? scriptPath : "", errors);
    if (!module || !errors.empty())
        return false;

    ScriptInstance instance;
    instance.module = module;
    instance.fields.assign(module->fields.size(), 0.0);
    instance.runtimeFields.resize(module->fields.size());
    instance.coroutines.reserve(16);
    for (size_t i = 0; i < module->fields.size(); ++i) {
        const uint16_t constant = module->fields[i].defaultConstIndex;
        const double value = constant < module->numberConstants.size()
            ? module->numberConstants[constant] : 0.0;
        instance.fields[i] = value;
        instance.runtimeFields[i].tag = Value::Tag::Number;
        instance.runtimeFields[i].number = value;
    }

    VM vm;
    if (!vm.RunMethod(instance, "Start", errors))
        return false;
    const int resultField = module->FindFieldIndex("result");
    auto result = [&]() -> double {
        return resultField >= 0 && static_cast<size_t>(resultField) < instance.fields.size()
            ? instance.fields[static_cast<size_t>(resultField)] : -9999.0;
    };
    if (result() != 31.0 || instance.coroutines.size() != 1) {
        errors.push_back("array regression: expected result=31 and one coroutine");
        return false;
    }

    vm.ResumeCoroutines(instance, 0.0f, errors);   // run to yield return null
    if (result() != 32.0) {
        errors.push_back("coroutine regression: first segment did not run");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.016f, errors); // consume one-frame wait
    if (result() != 32.0) {
        errors.push_back("coroutine regression: yield return null resumed too early");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.016f, errors); // run to WaitForSeconds
    if (result() != 42.0) {
        errors.push_back("coroutine regression: second segment did not run");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.02f, errors);
    vm.ResumeCoroutines(instance, 0.02f, errors);
    if (result() != 42.0) {
        errors.push_back("coroutine regression: WaitForSeconds resumed too early");
        return false;
    }
    vm.ResumeCoroutines(instance, 0.02f, errors);
    if (result() != 142.0 || !instance.coroutines.empty()) {
        errors.push_back("coroutine regression: completion or yield break failed");
        return false;
    }
    std::vector<std::string> boundsErrors;
    vm.RunMethod(instance, "BoundsError", boundsErrors);
    if (boundsErrors.empty()) {
        errors.push_back("array regression: out-of-range write was not diagnosed");
        return false;
    }
    return errors.empty();
}

} // namespace MipsyncEngine::Mips
