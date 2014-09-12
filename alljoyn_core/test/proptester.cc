/**
 * @file
 *
 * Program to test bus object properties.
 */

/*
 * Copyright (c) 2014 AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <assert.h>
#include <signal.h>
#include <stdio.h>

#include <qcc/Debug.h>
#include <qcc/Event.h>
#include <qcc/Mutex.h>
#include <qcc/String.h>
#include <qcc/StringUtil.h>
#include <qcc/Thread.h>
#include <qcc/Util.h>

#include <alljoyn/BusAttachment.h>
#include <alljoyn/BusListener.h>
#include <alljoyn/BusObject.h>
#include <alljoyn/ProxyBusObject.h>


using namespace ajn;
using namespace std;
using namespace qcc;


static volatile sig_atomic_t quit;


static const SessionPort PORT = 123;

static const SessionOpts SESSION_OPTS(SessionOpts::TRAFFIC_MESSAGES, false, SessionOpts::PROXIMITY_ANY, TRANSPORT_ANY);



static const char* propTesterInterfaceXML =
    "<node name=\"/org/alljoyn/Testing/PropertyTester\">"
    "  <interface name=\"org.alljoyn.Testing.PropertyTester\">"

    "    <property name=\"int32\" type=\"i\" access=\"readwrite\"/>"
    "    <property name=\"uint32\" type=\"u\" access=\"read\">"
    "      <annotation name=\"org.freedesktop.DBus.Property.EmitsChangedSignal\" value=\"true\"/>"
    "    </property>"
    "    <property name=\"string\" type=\"s\" access=\"write\"/>"
    "    <property name=\"sessionId\" type=\"u\" access=\"read\"/>"

    "  </interface>"
    "</node>";


class PropTesterObject : public BusObject, private Thread {
  public:
    PropTesterObject(BusAttachment& bus, const char* path, SessionId id, bool autoChange);
    ~PropTesterObject();

    void Set(int32_t v);
    void Set(uint32_t v);
    void Set(const char* v);

  private:

    const bool autoChange;
    int32_t int32Prop;    // RW property
    uint32_t uint32Prop;  // RO property
    String stringProp;    // RW property
    SessionId id;         // SessionId and RO property
    bool stop;
    Mutex lock;

    QStatus Get(const char* ifcName, const char* propName, MsgArg& val);
    QStatus Set(const char* ifcName, const char* propName, MsgArg& val);

    // Thread methods
    ThreadReturn STDCALL Run(void* arg);
};

PropTesterObject::PropTesterObject(BusAttachment& bus, const char* path, SessionId id, bool autoChange) :
    BusObject(bus, path),
    autoChange(autoChange),
    int32Prop(0),
    uint32Prop(0),
    stringProp(path),
    id(id),
    stop(false)
{
    const InterfaceDescription* ifc = bus.GetInterface("org.alljoyn.Testing.PropertyTester");
    if (!ifc) {
        bus.CreateInterfacesFromXml(propTesterInterfaceXML);
        ifc = bus.GetInterface("org.alljoyn.Testing.PropertyTester");
    }
    assert(ifc);

    AddInterface(*ifc);

    if (autoChange) {
        Start();
    }
}

PropTesterObject::~PropTesterObject()
{
    lock.Lock();
    stop = true;
    lock.Unlock();

    if (autoChange) {
        Stop();
        Join();
    }
}

void PropTesterObject::Set(int32_t v)
{
    int32Prop = v;
    MsgArg val("i", v);
    EmitPropChanged("org.alljoyn.Testing.PropertyTester", "int32", val, id);
}

void PropTesterObject::Set(uint32_t v)
{
    uint32Prop = v;
    MsgArg val("u", v);
    EmitPropChanged("org.alljoyn.Testing.PropertyTester", "uint32", val, id);
}

void PropTesterObject::Set(const char* v)
{
    stringProp = v;
    MsgArg val("s", v);
    EmitPropChanged("org.alljoyn.Testing.PropertyTester", "string", val, id);
}

QStatus PropTesterObject::Get(const char* ifcName, const char* propName, MsgArg& val)
{
    QStatus status = ER_BUS_NO_SUCH_PROPERTY;
    if (strcmp(ifcName, "org.alljoyn.Testing.PropertyTester") == 0) {
        lock.Lock();
        if (strcmp(propName, "int32") == 0) {
            val.Set("i", int32Prop);
            status = ER_OK;
            QCC_SyncPrintf("Get property %s (%d) at %s\n", propName, int32Prop, GetPath());

        } else if (strcmp(propName, "uint32") == 0) {
            val.Set("u", uint32Prop);
            status = ER_OK;
            QCC_SyncPrintf("Get property %s (%u) at %s\n", propName, uint32Prop, GetPath());

        } else if (strcmp(propName, "string") == 0) {
            val.Set("s", stringProp.c_str());
            status = ER_OK;
            QCC_SyncPrintf("Get property %s (%s) at %s\n", propName, stringProp.c_str(), GetPath());

        } else if (strcmp(propName, "session") == 0) {
            val.Set("u", id);
            status = ER_OK;
            QCC_SyncPrintf("Get property %s (%u) at %s\n", propName, id, GetPath());
        }
        lock.Unlock();
    }
    return status;
}

QStatus PropTesterObject::Set(const char* ifcName, const char* propName, MsgArg& val)
{
    QStatus status = ER_BUS_NO_SUCH_PROPERTY;
    if (strcmp(ifcName, "org.alljoyn.Testing.PropertyTester") == 0) {
        lock.Lock();
        if (strcmp(propName, "int32") == 0) {
            val.Get("i", &int32Prop);
            EmitPropChanged(ifcName, propName, val, id);
            status = ER_OK;
            QCC_SyncPrintf("Set property %s (%d) at %s\n", propName, int32Prop, GetPath());

        } else if (strcmp(propName, "uint32") == 0) {
            val.Get("u", &uint32Prop);
            EmitPropChanged(ifcName, propName, val, id);
            status = ER_OK;
            QCC_SyncPrintf("Set property %s (%u) at %s\n", propName, uint32Prop, GetPath());

        } else if (strcmp(propName, "string") == 0) {
            const char* s;
            val.Get("s", &s);
            stringProp = s;
            EmitPropChanged(ifcName, propName, val, id);
            status = ER_OK;
            QCC_SyncPrintf("Set property %s (%s) at %s\n", propName, stringProp.c_str(), GetPath());

        } else if (strcmp(propName, "session") == 0) {
            status = ER_OK;  // ignore
            QCC_SyncPrintf("Set property %s (%u) at %s (IGNORED) \n", propName, id, GetPath());
        }
        lock.Unlock();
    }
    return status;
}


ThreadReturn STDCALL PropTesterObject::Run(void* arg)
{
    Event dummy;
    lock.Lock();
    while (!IsStopping()) {
        lock.Unlock();
        Event::Wait(dummy, 2000);
        lock.Lock();
        ++uint32Prop;
        MsgArg val("u", uint32Prop);
        QCC_SyncPrintf("Updating uint32: %u\n", uint32Prop);
        EmitPropChanged("org.alljoyn.Testing.PropertyTester", "uint32", val, id);
    }
    lock.Unlock();

    return 0;
}




class _PropTesterProxyObject : public ProxyBusObject, private ProxyBusObject::Listener {
  public:
    _PropTesterProxyObject(BusAttachment& bus, const String& service, const String& path, SessionId sessionId);
    ~_PropTesterProxyObject();

    QStatus Set(int32_t v);
    QStatus Set(uint32_t v);
    QStatus Set(const char* v);

    QStatus Get(int32_t& v);
    QStatus Get(uint32_t& v);
    QStatus Get(const char*& v);

  private:

    void PropertyChangedHandler(ProxyBusObject* obj, const char* ifaceName, const char* propName, const MsgArg* value, void* context);

};

typedef ManagedObj<_PropTesterProxyObject> PropTesterProxyObject;



_PropTesterProxyObject::_PropTesterProxyObject(BusAttachment& bus, const String& service, const String& path, SessionId sessionId) :
    ProxyBusObject(bus, service.c_str(), path.c_str(), sessionId)
{
    const InterfaceDescription* ifc = bus.GetInterface("org.alljoyn.Testing.PropertyTester");
    if (!ifc) {
        bus.CreateInterfacesFromXml(propTesterInterfaceXML);
        ifc = bus.GetInterface("org.alljoyn.Testing.PropertyTester");
    }
    assert(ifc);

    AddInterface(*ifc);

    RegisterPropertyChangedHandler("org.alljoyn.Testing.PropertyTester", "int32", this,
                                   reinterpret_cast<ProxyBusObject::Listener::PropertyChanged>(&_PropTesterProxyObject::PropertyChangedHandler),
                                   NULL);
    RegisterPropertyChangedHandler("org.alljoyn.Testing.PropertyTester", "uint32", this,
                                   reinterpret_cast<ProxyBusObject::Listener::PropertyChanged>(&_PropTesterProxyObject::PropertyChangedHandler),
                                   NULL);
    RegisterPropertyChangedHandler("org.alljoyn.Testing.PropertyTester", "string", this,
                                   reinterpret_cast<ProxyBusObject::Listener::PropertyChanged>(&_PropTesterProxyObject::PropertyChangedHandler),
                                   NULL);

}

_PropTesterProxyObject::~_PropTesterProxyObject()
{
    UnregisterPropertyChangedHandler("org.alljoyn.Testing.PropertyTester", "int32", this,
                                     reinterpret_cast<ProxyBusObject::Listener::PropertyChanged>(&_PropTesterProxyObject::PropertyChangedHandler));
    UnregisterPropertyChangedHandler("org.alljoyn.Testing.PropertyTester", "uint32", this,
                                     reinterpret_cast<ProxyBusObject::Listener::PropertyChanged>(&_PropTesterProxyObject::PropertyChangedHandler));
    UnregisterPropertyChangedHandler("org.alljoyn.Testing.PropertyTester", "string", this,
                                     reinterpret_cast<ProxyBusObject::Listener::PropertyChanged>(&_PropTesterProxyObject::PropertyChangedHandler));
}


QStatus _PropTesterProxyObject::Set(int32_t v)
{
    return SetProperty("org.alljoyn.Testing.PropertyTester", "int32", v);
}

QStatus _PropTesterProxyObject::Set(uint32_t v)
{
    return SetProperty("org.alljoyn.Testing.PropertyTester", "uint32", v);
}

QStatus _PropTesterProxyObject::Set(const char* v)
{
    return SetProperty("org.alljoyn.Testing.PropertyTester", "string", v);
}

QStatus _PropTesterProxyObject::Get(int32_t& v)
{
    MsgArg val;
    QStatus status = GetProperty("org.alljoyn.Testing.PropertyTester", "int32", val);
    if (status == ER_OK) {
        status = val.Get("i", &v);
    }
    return status;
}

QStatus _PropTesterProxyObject::Get(uint32_t& v)
{
    MsgArg val;
    QStatus status = GetProperty("org.alljoyn.Testing.PropertyTester", "uint32", val);
    if (status == ER_OK) {
        status = val.Get("u", &v);
    }
    return status;
}

QStatus _PropTesterProxyObject::Get(const char*& v)
{
    MsgArg val;
    QStatus status = GetProperty("org.alljoyn.Testing.PropertyTester", "string", val);
    if (status == ER_OK) {
        status = val.Get("s", &v);
    }
    return status;
}

void _PropTesterProxyObject::PropertyChangedHandler(ProxyBusObject* obj, const char* ifaceName, const char* propName, const MsgArg* value, void* context)
{
    String valStr = value->ToString();

    QCC_SyncPrintf("Property Changed event: %s = %s (bus name: %s   object path: %s)\n",
                   propName, valStr.c_str(), obj->GetServiceName().c_str(), obj->GetPath().c_str());
}


class App {
  public:
    virtual ~App() { }
};


class Service : public App, private SessionPortListener, private SessionListener {
  public:
    Service(BusAttachment& bus);
    ~Service();

  private:
    BusAttachment& bus;
    multimap<SessionId, PropTesterObject*> objects;
    SessionPort port;

    void Add(SessionId id, bool autoUpdate);

    // SessionPortListener methods
    bool AcceptSessionJoiner(SessionPort sessionPort, const char* joiner, const SessionOpts& opts) { return true; }
    void SessionJoined(SessionPort sessionPort, SessionId id, const char* joiner);

    // SessionListener methdods
    void SessionLost(SessionId sessionId);
};

Service::Service(BusAttachment& bus) :
    bus(bus),
    port(PORT)
{
    Add(0, false);
    Add(0, true);
    QStatus status = bus.BindSessionPort(port, SESSION_OPTS, *this);
    if (status != ER_OK) {
        QCC_SyncPrintf("Failed to bind session port \"%u\": %s\n", port, QCC_StatusText(status));
        exit(1);
    }
}

Service::~Service()
{
    bus.UnbindSessionPort(port);
    while (!objects.empty()) {
        delete objects.begin()->second;
        objects.erase(objects.begin());
    }
}

void Service::Add(SessionId id, bool autoUpdate)
{
    String path = "/org/alljoyn/Testing/PropertyTester/";
    path += U32ToString(id);
    if (autoUpdate) {
        path += "/a";
    } else {
        path += "/b";
    }
    PropTesterObject* obj = new PropTesterObject(bus, path.c_str(), id, autoUpdate);
    pair<SessionId, PropTesterObject*> item(id, obj);
    objects.insert(item);
    bus.RegisterBusObject(*obj);
}

void Service::SessionJoined(SessionPort sessionPort, SessionId id, const char* joiner)
{
    bus.SetSessionListener(id, this);
    Add(id, false);
    Add(id, true);
}

void Service::SessionLost(SessionId sessionId)
{
    multimap<SessionId, PropTesterObject*>::iterator it;
    it = objects.find(sessionId);
    while (it != objects.end()) {
        bus.UnregisterBusObject(*(it->second));
        delete it->second;
        objects.erase(it);
        it = objects.find(sessionId);
    }
}


class Client : public App, private BusListener, private BusAttachment::JoinSessionAsyncCB, private Thread {
  public:
    Client(BusAttachment& bus);
    ~Client();

  private:
    BusAttachment& bus;
    map<SessionId, PropTesterProxyObject> aObjects;  // auto updated objects
    map<SessionId, PropTesterProxyObject> bObjects;  // get/set test objects
    set<String> foundNames;
    Mutex lock;
    Event newServiceFound;

    void Add(const String& name, SessionId id, bool aObj);
    void TestProps(SessionId id);

    // BusListener methods
    void FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix);
    void LostAdvertisedName(const char* name, TransportMask transport, const char* namePrefix);

    //BusAttachment::JoinsessionAsyncCB methods
    void JoinSessionCB(QStatus status, SessionId sessionId, const SessionOpts& opts, void* context);

    // Thread methods
    ThreadReturn STDCALL Run(void* arg);
};

Client::Client(BusAttachment& bus) :
    bus(bus)
{
    bus.RegisterBusListener(*this);
    Start();
}

Client::~Client()
{
    while (!aObjects.empty()) {
        aObjects.erase(aObjects.begin());
    }
    while (!bObjects.empty()) {
        bObjects.erase(bObjects.begin());
    }
    Stop();
    Join();
    bus.UnregisterBusListener(*this);
}


void Client::Add(const String& name, SessionId id, bool aObj)
{
    String path = "/org/alljoyn/Testing/PropertyTester/";
    path += U32ToString(id);
    if (aObj) {
        path += "/a";
    } else {
        path += "/b";
    }
    PropTesterProxyObject obj(bus, name, path, id);
    pair<SessionId, PropTesterProxyObject> item(id, obj);
    if (aObj) {
        aObjects.insert(item);
    } else {
        bObjects.insert(item);
    }
}



void Client::FoundAdvertisedName(const char* name, TransportMask transport, const char* namePrefix)
{
    QCC_SyncPrintf("FoundAdvertisedName: \"%s\"\n", name);
    String nameStr = name;
    lock.Lock();
    set<String>::iterator it = foundNames.find(nameStr);
    if (it == foundNames.end()) {
        QCC_SyncPrintf("Joining session with %s\n", name);
        bus.JoinSessionAsync(name, PORT, NULL, SESSION_OPTS, this, new String(nameStr));
        foundNames.insert(nameStr);
    }
    lock.Unlock();
}

void Client::LostAdvertisedName(const char* name, TransportMask transport, const char* namePrefix)
{
    QCC_SyncPrintf("LostAdvertisedName: \"%s\"\n", name);
    String nameStr = name;
    lock.Lock();
    set<String>::iterator it = foundNames.find(nameStr);
    if (it != foundNames.end()) {
        foundNames.erase(it);
    }
    lock.Unlock();
}

void Client::JoinSessionCB(QStatus status, SessionId sessionId, const SessionOpts& opts, void* context)
{
    String* nameStr = reinterpret_cast<String*>(context);
    QCC_SyncPrintf("JoinSessionCB: name = %s   status = %s\n", nameStr->c_str(), QCC_StatusText(status));
    if (status == ER_OK) {
        Add(*nameStr, 0, false);
        Add(*nameStr, 0, true);
        Add(*nameStr, sessionId, false);
        Add(*nameStr, sessionId, true);
        Alert(static_cast<uint32_t>(sessionId));
    }
    delete nameStr;
}


void Client::TestProps(SessionId id)
{
    lock.Lock();
    map<SessionId, PropTesterProxyObject>::iterator it = bObjects.find(id);
    if (it != bObjects.end()) {
        PropTesterProxyObject obj = it->second;
        lock.Unlock();

        uint32_t rand = Rand32();
        int32_t int32 = -12345678;
        uint32_t uint32 = 12345678;
        const char* string;

        QStatus status = obj->Get(int32);
        QCC_SyncPrintf("Got int32 value: %d   from %s - %s: status = %s: %s\n", int32, obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (status == ER_OK) ? "PASS" : "FAIL");
        status = obj->Get(uint32);
        QCC_SyncPrintf("Got uint32 value: %u   from %s - %s: status = %s: %s\n", uint32, obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (status == ER_OK) ? "PASS" : "FAIL");
        string = NULL;
        status = obj->Get(string);
        QCC_SyncPrintf("Got string value: \"%s\"   from %s - %s: status = %s: %s\n", string, obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (status != ER_OK) ? "PASS" : "FAIL");

        status = obj->Set(static_cast<int32_t>(rand));
        QCC_SyncPrintf("Set int32 value: %d   from %s - %s: status = %s: %s\n", static_cast<int32_t>(rand), obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (status == ER_OK) ? "PASS" : "FAIL");
        status = obj->Set(rand);
        QCC_SyncPrintf("Set uint32 value: %u   from %s - %s: status = %s: %s\n", rand, obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (status != ER_OK) ? "PASS" : "FAIL");
        status = obj->Set(bus.GetUniqueName().c_str());
        QCC_SyncPrintf("Set string value: \"%s\"   from %s - %s: status = %s: %s\n", bus.GetUniqueName().c_str(), obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (status == ER_OK) ? "PASS" : "FAIL");

        status = obj->Get(int32);
        QCC_SyncPrintf("Got int32 value: %d   from %s - %s: status = %s: %s\n", int32, obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (int32 == static_cast<int32_t>(rand)) ? "PASS" : "FAIL");
        status = obj->Get(uint32);
        QCC_SyncPrintf("Got uint32 value: %u   from %s - %s: status = %s: %s\n", uint32, obj->GetServiceName().c_str(), obj->GetPath().c_str(), QCC_StatusText(status), (uint32 != rand) ? "PASS" : "FAIL");


    } else {
        lock.Unlock();
    }
}


ThreadReturn STDCALL Client::Run(void* arg)
{
    Event dummy;
    lock.Lock();
    while (!IsStopping()) {
        lock.Unlock();
        Event::Wait(dummy);
        if (!IsStopping()) {
            GetStopEvent().ResetEvent();
            SessionId newId = static_cast<SessionId>(GetAlertCode());
            TestProps(0);
            TestProps(newId);
        }
        lock.Lock();
    }
    lock.Unlock();

    return 0;
}



void SignalHandler(int sig) {
    if ((sig == SIGINT) ||
        (sig == SIGTERM)) {
        quit = 1;
    }
}


void Usage()
{
    printf("proptester: [ -c ] [ -n <NAME> ] [ -s <SECONDS> ]\n"
           "    -c            Run as client (runs as service by default).\n"
           "    -n <NAME>     Use <NAME> for well known bus name.\n");
}


int main(int argc, char** argv)
{
    String serviceName = "org.alljoyn.Testing.PropertyTester";
    bool client = false;

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0) {
            client = true;
        } else if (strcmp(argv[i], "-n") == 0) {
            ++i;
            if (i == argc) {
                printf("option %s requires a parameter\n", argv[i - 1]);
                Usage();
                exit(1);
            } else {
                serviceName = argv[i];
            }
        } else if (strcmp(argv[i], "-h") == 0) {
            Usage();
            exit(1);
        } else {
            Usage();
            exit(1);
        }
    }


    signal(SIGINT, SignalHandler);
    signal(SIGTERM, SignalHandler);

    quit = 0;

    QStatus status;
    int ret = 0;
    BusAttachment bus("ProperyTester", true);
    Environ* env = Environ::GetAppEnviron();
    String connSpec = env->Find("DBUS_STARTER_ADDRESS");

    if (connSpec.empty()) {
#ifdef _WIN32
        connSpec = env->Find("BUS_ADDRESS", "tcp:addr=127.0.0.1,port=9956");
#else
        connSpec = env->Find("BUS_ADDRESS", "unix:abstract=alljoyn");
#endif
    }

    status = bus.Start();
    if (status != ER_OK) {
        printf("Failed to start bus attachment: %s\n", QCC_StatusText(status));
        exit(1);
    }

    status = bus.Connect(connSpec.c_str());
    if (status != ER_OK) {
        printf("Failed to connect to \"%s\": %s\n", connSpec.c_str(), QCC_StatusText(status));
        exit(1);
    }

    App* app;

    if (client) {
        app = new Client(bus);
        status = bus.FindAdvertisedName(serviceName.c_str());
        if (status != ER_OK) {
            printf("Failed to find name to \"%s\": %s\n", serviceName.c_str(), QCC_StatusText(status));
            ret = 2;
            goto exit;
        }
    } else {
        serviceName += ".A" + bus.GetGlobalGUIDString();

        app = new Service(bus);
        status = bus.RequestName(serviceName.c_str(), DBUS_NAME_FLAG_REPLACE_EXISTING | DBUS_NAME_FLAG_DO_NOT_QUEUE);
        if (status != ER_OK) {
            printf("Failed to request name to \"%s\": %s\n", serviceName.c_str(), QCC_StatusText(status));
            ret = 2;
            goto exit;
        }
        status = bus.AdvertiseName(serviceName.c_str(), TRANSPORT_ANY);
        if (status != ER_OK) {
            printf("Failed to request name to \"%s\": %s\n", serviceName.c_str(), QCC_StatusText(status));
            ret = 2;
            goto exit;
        }
    }

    while (!quit) {
        qcc::Sleep(100);
    }

    printf("QUITTING\n");

exit:
    if (client) {
        bus.CancelFindAdvertisedName(serviceName.c_str());
        bus.Disconnect(connSpec.c_str());
    } else {
        bus.CancelAdvertiseName(serviceName.c_str(), TRANSPORT_ANY);
        bus.ReleaseName(serviceName.c_str());
    }

    delete app;

    bus.Stop();
    bus.Join();

    return ret;
}