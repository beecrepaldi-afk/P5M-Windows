// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL
// P5M for Windows: network facts for Report a problem.

#include "windowsNetworkReport.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QString>
#include <QStringList>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <netlistmgr.h>
#include <netfw.h>
#include <oleauto.h>

#include <vector>

namespace {

template<typename T> struct ComPtr
{
    T *p = nullptr;
    ~ComPtr() { if (p) p->Release(); }
    T **operator&() { return &p; }
    T *operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

QString profileNames(long profiles)
{
    if (profiles == NET_FW_PROFILE2_ALL || (profiles & 7) == 7)
        return QStringLiteral("all");
    QStringList names;
    if (profiles & NET_FW_PROFILE2_DOMAIN) names << QStringLiteral("Domain");
    if (profiles & NET_FW_PROFILE2_PRIVATE) names << QStringLiteral("Private");
    if (profiles & NET_FW_PROFILE2_PUBLIC) names << QStringLiteral("Public");
    return names.isEmpty() ? QStringLiteral("none") : names.join(QLatin1Char('+'));
}

QString protocolName(long protocol)
{
    switch (protocol) {
    case 6: return QStringLiteral("TCP");
    case 17: return QStringLiteral("UDP");
    case 256: return QStringLiteral("any");
    default: return QString::number(protocol);
    }
}

QString expandPath(const BSTR path)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = ExpandEnvironmentStringsW(path, buf, sizeof(buf) / sizeof(buf[0]));
    const QString expanded = (n > 0 && n <= sizeof(buf) / sizeof(buf[0])) ? QString::fromWCharArray(buf) : QString::fromWCharArray(path);
    return QDir::cleanPath(QDir::fromNativeSeparators(expanded));
}

void networkCategories(QStringList &out)
{
    ComPtr<INetworkListManager> nlm;
    if (FAILED(CoCreateInstance(__uuidof(NetworkListManager), nullptr, CLSCTX_ALL, __uuidof(INetworkListManager), reinterpret_cast<void **>(&nlm)))) {
        out << QStringLiteral("Network category: unknown");
        return;
    }
    ComPtr<IEnumNetworks> networks;
    if (FAILED(nlm->GetNetworks(NLM_ENUM_NETWORK_CONNECTED, &networks)) || !networks) {
        out << QStringLiteral("Network category: unknown");
        return;
    }
    int count = 0;
    for (;;) {
        ComPtr<INetwork> network;
        ULONG fetched = 0;
        if (networks->Next(1, &network, &fetched) != S_OK || fetched == 0)
            break;
        NLM_NETWORK_CATEGORY category = NLM_NETWORK_CATEGORY_PUBLIC;
        network->GetCategory(&category);
        NLM_CONNECTIVITY connectivity = NLM_CONNECTIVITY_DISCONNECTED;
        network->GetConnectivity(&connectivity);
        const char *name = category == NLM_NETWORK_CATEGORY_PRIVATE ? "Private"
            : category == NLM_NETWORK_CATEGORY_DOMAIN_AUTHENTICATED ? "Domain" : "Public";
        out << QStringLiteral("Connected network %1: %2%3").arg(++count).arg(QString::fromLatin1(name),
            (connectivity & NLM_CONNECTIVITY_IPV4_INTERNET) ? QStringLiteral(", internet") : QString());
    }
    if (count == 0)
        out << QStringLiteral("Connected networks: none");
}

void firewall(QStringList &out)
{
    ComPtr<INetFwPolicy2> policy;
    if (FAILED(CoCreateInstance(__uuidof(NetFwPolicy2), nullptr, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), reinterpret_cast<void **>(&policy)))) {
        out << QStringLiteral("Firewall: unknown (third-party firewall?)");
        return;
    }
    long current = 0;
    policy->get_CurrentProfileTypes(&current);
    out << QStringLiteral("Firewall active profile: %1").arg(profileNames(current));
    const NET_FW_PROFILE_TYPE2 profiles[] = { NET_FW_PROFILE2_PRIVATE, NET_FW_PROFILE2_PUBLIC, NET_FW_PROFILE2_DOMAIN };
    for (NET_FW_PROFILE_TYPE2 profile : profiles) {
        VARIANT_BOOL enabled = VARIANT_FALSE, block_all = VARIANT_FALSE;
        NET_FW_ACTION inbound = NET_FW_ACTION_BLOCK;
        policy->get_FirewallEnabled(profile, &enabled);
        policy->get_BlockAllInboundTraffic(profile, &block_all);
        policy->get_DefaultInboundAction(profile, &inbound);
        out << QStringLiteral("Firewall %1: %2%3, default inbound %4").arg(profileNames(profile),
            enabled ? QStringLiteral("on") : QStringLiteral("off"),
            block_all ? QStringLiteral(", BLOCKS ALL INCOMING") : QString(),
            inbound == NET_FW_ACTION_ALLOW ? QStringLiteral("allow") : QStringLiteral("block"));
    }

    ComPtr<INetFwRules> rules;
    ComPtr<IUnknown> unknown;
    ComPtr<IEnumVARIANT> it;
    if (FAILED(policy->get_Rules(&rules)) || !rules || FAILED(rules->get__NewEnum(&unknown)) || !unknown
            || FAILED(unknown->QueryInterface(__uuidof(IEnumVARIANT), reinterpret_cast<void **>(&it)))) {
        out << QStringLiteral("Firewall rules: unreadable");
        return;
    }
    const QString self = QDir::cleanPath(QCoreApplication::applicationFilePath());
    const QString self_name = QFileInfo(self).fileName();
    int ours = 0, other_copies = 0;
    for (;;) {
        VARIANT var;
        VariantInit(&var);
        ULONG fetched = 0;
        if (it->Next(1, &var, &fetched) != S_OK || fetched == 0)
            break;
        ComPtr<INetFwRule> rule;
        if (var.vt == VT_DISPATCH && var.pdispVal)
            var.pdispVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void **>(&rule));
        VariantClear(&var);
        if (!rule)
            continue;
        BSTR app = nullptr;
        if (FAILED(rule->get_ApplicationName(&app)) || !app)
            continue;
        const QString path = expandPath(app);
        SysFreeString(app);
        const bool same = path.compare(self, Qt::CaseInsensitive) == 0;
        if (!same) {
            if (QFileInfo(path).fileName().compare(self_name, Qt::CaseInsensitive) == 0)
                other_copies++;
            continue;
        }
        NET_FW_RULE_DIRECTION direction = NET_FW_RULE_DIR_IN;
        NET_FW_ACTION action = NET_FW_ACTION_ALLOW;
        long rule_profiles = 0, protocol = 256;
        VARIANT_BOOL enabled = VARIANT_FALSE;
        rule->get_Direction(&direction);
        rule->get_Action(&action);
        rule->get_Profiles(&rule_profiles);
        rule->get_Protocol(&protocol);
        rule->get_Enabled(&enabled);
        out << QStringLiteral("Firewall rule for this P5M.exe: %1 %2 %3, profiles %4%5").arg(
            action == NET_FW_ACTION_ALLOW ? QStringLiteral("ALLOW") : QStringLiteral("BLOCK"),
            direction == NET_FW_RULE_DIR_IN ? QStringLiteral("in") : QStringLiteral("out"),
            protocolName(protocol), profileNames(rule_profiles),
            enabled ? QString() : QStringLiteral(" (disabled)"));
        ours++;
    }
    if (ours == 0)
        out << QStringLiteral("Firewall rules for this P5M.exe: none (Windows asks on first use; a packaged install uses its own rules)");
    if (other_copies > 0)
        out << QStringLiteral("Firewall rules for other copies of %1: %2").arg(self_name).arg(other_copies);
}

QString adapterType(IFTYPE type)
{
    switch (type) {
    case IF_TYPE_ETHERNET_CSMACD: return QStringLiteral("Ethernet");
    case IF_TYPE_IEEE80211: return QStringLiteral("Wi-Fi");
    case IF_TYPE_PPP: return QStringLiteral("PPP/VPN");
    case IF_TYPE_TUNNEL: return QStringLiteral("tunnel");
    case IF_TYPE_PROP_VIRTUAL: return QStringLiteral("virtual");
    default: return QStringLiteral("type %1").arg(type);
    }
}

void adapters(QStringList &out)
{
    ULONG size = 16 * 1024;
    std::vector<unsigned char> buf;
    ULONG rc = ERROR_BUFFER_OVERFLOW;
    const ULONG flags = GAA_FLAG_INCLUDE_GATEWAYS | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER;
    for (int tries = 0; tries < 3 && rc == ERROR_BUFFER_OVERFLOW; tries++) {
        buf.resize(size);
        rc = GetAdaptersAddresses(AF_UNSPEC, flags, nullptr, reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()), &size);
    }
    if (rc != NO_ERROR) {
        out << QStringLiteral("Adapters: unreadable (%1)").arg(rc);
        return;
    }
    for (auto a = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buf.data()); a; a = a->Next) {
        if (a->OperStatus != IfOperStatusUp || a->IfType == IF_TYPE_SOFTWARE_LOOPBACK)
            continue;
        bool ipv4 = false;
        for (auto u = a->FirstUnicastAddress; u; u = u->Next)
            if (u->Address.lpSockaddr && u->Address.lpSockaddr->sa_family == AF_INET)
                ipv4 = true;
        out << QStringLiteral("Adapter up: \"%1\" (%2)%3%4").arg(QString::fromWCharArray(a->Description), adapterType(a->IfType),
            ipv4 ? QStringLiteral(", IPv4") : QStringLiteral(", no IPv4"),
            a->FirstGatewayAddress ? QStringLiteral(", default gateway") : QString());
    }
}

} // namespace

QString windowsNetworkReport()
{
    QStringList out;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    networkCategories(out);
    firewall(out);
    if (SUCCEEDED(init))
        CoUninitialize();
    adapters(out);
    return out.join(QLatin1Char('\n'));
}
