/**
 * Registration.cpp
 * Registro del plugin SoapyFlexRadio en SoapySDR.
 */

#include "SoapyFlexRadio.hpp"
#include <SoapySDR/Registry.hpp>
#include <SoapySDR/Modules.hpp>

static SoapySDR::KwargsList findFlexRadio(const SoapySDR::Kwargs &args)
{
    SoapySDR::KwargsList results;

    std::string port = args.count("udp_port") ? args.at("udp_port") : "5901";
    std::string rate = args.count("rate")     ? args.at("rate")     : "192000";
    std::string freq = args.count("freq")     ? args.at("freq")     : "14200000";
    std::string host = args.count("host")     ? args.at("host")     : "192.168.0.208";

    SoapySDR::Kwargs found;
    found["driver"]   = "FlexRadio";
    found["label"]    = "FlexRadio FLEX-6600 (via smartsdr-iqtransfer) UDP:" + port;
    found["udp_port"] = port;
    found["rate"]     = rate;
    found["freq"]     = freq;
    found["host"]     = host;
    results.push_back(found);

    return results;
}

static SoapySDR::Device* makeFlexRadio(const SoapySDR::Kwargs &args)
{
    return new SoapyFlexRadio(args);
}

static SoapySDR::Registry registerFlexRadio(
    "FlexRadio",
    &findFlexRadio,
    &makeFlexRadio,
    SOAPY_SDR_ABI_VERSION
);

// Exportar función explícita para que MSVC no optimice los símbolos
extern "C" __declspec(dllexport) const char* SoapyFlexRadio_getVersion(void)
{
    return "2.0.0";
}
