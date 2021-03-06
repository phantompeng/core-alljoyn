/**
 * @file
 * AllJoyn-Daemon Config file database class
 */

/******************************************************************************
 * Copyright AllSeen Alliance. All rights reserved.
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
 ******************************************************************************/
#include <qcc/platform.h>

#include <errno.h>
#include <sys/types.h>
#if !defined(QCC_OS_GROUP_WINDOWS)
#include <pwd.h>
#endif

#include <list>
#include <map>

#include <qcc/String.h>
#include <qcc/StringSource.h>
#include <qcc/StringUtil.h>
#include <qcc/FileStream.h>
#include <qcc/Logger.h>
#include <qcc/String.h>
#include <qcc/Util.h>
#include <qcc/XmlElement.h>

#include "ConfigDB.h"
#ifdef ENABLE_POLICYDB
#include "PolicyDB.h"
#endif

using namespace ajn;
using namespace qcc;
using namespace std;


/**
 * Coverts the path to an absolute path based on various criteria.  For POSIX
 * platforms, it interprets the "~/" to mean the user's home directory and
 * "~user/" as the home directory of "user" and returns the absolute path with
 * "~/" or "~user/" replaced with the path to the user's home directory.  If
 * the given path does not start with "/", then the base path name extracted
 * from fileName to generate the absolute path name.
 *
 * @param path         Path name to be made into an absolute path
 * @param srcFileName  [optional] Absolute path to a file that will be used as the
 *                     basis for generating an absolute path when given a relative
 *                     path.
 */
static String ExpandPath(const String& path, const String& srcFileName = String::Empty)
{
#if defined(QCC_OS_GROUP_WINDOWS)
#define PATH_SEP "/\\"
    bool absolute = (path.empty() || (path[0] == '/') || (path[0] == '\\') || path[1] == ':');
#else
#define PATH_SEP '/'
    bool absolute = path.empty() || (path[0] == '/');
#endif

    if (absolute) {
        return path;
#if defined(QCC_OS_GROUP_POSIX)
    } else if (path[0] == '~') {
        /*
         * A path starting with "~" indicates that it is relative to the home
         * directory.  This nomenclature comes from UNIX shells.  What is not
         * as well known, is that "~username" indicates the home directory of
         * "username" rather than home directory of the current user if "~" is
         * specified by itself.
         */
        size_t position = path.find_first_of(PATH_SEP);
        String user = path.substr(1, position - 1);
        String home;
        struct passwd* pwd = NULL;
        if (user.empty()) {
            home = getenv("HOME");
            /* If HOME is not set get the user's home directory from the /etc/passwd */
            if (home.empty()) {
                pwd = getpwuid(getuid());
            }
        } else {
            pwd = getpwnam(user.c_str());
        }
        if (pwd) {
            home = pwd->pw_dir;
        }
        return home + path.substr(position);
#endif
    } else if (srcFileName.empty()) {
        // Use path as is.
        return path;
    } else {
        // A path relative to the currently processed file
        return srcFileName.substr(0, srcFileName.find_last_of(PATH_SEP) + 1) + path;
    }

#undef PATH_SEP
}

ConfigDB* ConfigDB::singleton = NULL;

#ifdef ENABLE_POLICYDB
void ConfigDB::NameOwnerChanged(const String& alias,
                                const String* oldOwner,
                                SessionOpts::NameTransferType oldOwnerNameTransfer,
                                const String* newOwner,
                                SessionOpts::NameTransferType newOwnerNameTransfer)
{
    rwlock.RDLock();
    PolicyDB policy = db->policyDB;
    rwlock.Unlock();
    policy->NameOwnerChanged(alias,
                             oldOwner, oldOwnerNameTransfer,
                             newOwner, newOwnerNameTransfer);
}
#endif

ConfigDB::ConfigDB(const String defaultXml, const String srcFileName) :
    defaultXml(defaultXml), fileName(srcFileName), db(new DB()), stopping(false)
{
    QCC_ASSERT(!singleton);
    if (!singleton) {
        singleton = this;
    }
}

ConfigDB::~ConfigDB()
{
    delete db;
    singleton = NULL;
}

bool ConfigDB::LoadConfig(Bus* bus)
{
    if (stopping) {
        return false;
    }

    StringSource defaultSrc(defaultXml);
    DB* newDb = new DB();
    bool success = true;

    /*
     * The default config XML may have multiple <busconfig> root tags.
     * Strictly speaking this is not valid XML, but it is convenient for
     * composing default values with an internal configuration.  Because of
     * this structure, we need to process defaultSrc until all the bytes are
     * read.
     */
    while (success && (defaultSrc.Remaining() > 0)) {
        success = newDb->ParseSource("<default>", defaultSrc);
    }

    if (!fileName.empty()) {
        success = newDb->ParseFile(ExpandPath(fileName));
    }

    newDb->Finalize(bus);

    if (success) {
        rwlock.WRLock();
        DB* old = db;
        db = newDb;
        rwlock.Unlock();
        delete old;
    } else {
        delete newDb;
    }
    return success;
}


bool ConfigDB::DB::ParseSource(const String& srcFileName, Source& src)
{
    XmlParseContext xmlParseCtx(src);
    const XmlElement* root = xmlParseCtx.GetRoot();
    bool success;

    success = XmlElement::Parse(xmlParseCtx) == ER_OK;
    if (success) {
        if (root->GetName() == "busconfig") {
            success = ProcessBusconfig(srcFileName, *root);
        } else {
            Log(LOG_ERR, "Error processing \"%s\": Unknown tag found at top level: <%s>\n",
                srcFileName.c_str(), root->GetName().c_str());
            success = false;
        }
    } else {
        Log(LOG_ERR, "File \"%s\" contains invalid XML constructs.\n", srcFileName.c_str());
    }

    return success;
}


bool ConfigDB::DB::ParseFile(const String& srcFileName, bool ignoreMissing)
{
    bool success = true;
    FileSource fs(srcFileName.c_str());

    if (fs.IsValid()) {
        success = ParseSource(srcFileName, fs);
    } else if (!ignoreMissing) {
        Log(LOG_ERR, "Failed to open \"%s\": %s\n", srcFileName.c_str(), strerror(errno));
        success = false;
    }

    return success;
}


bool ConfigDB::DB::ProcessAuth(const String& srcFileName, const XmlElement& auth)
{
    QCC_UNUSED(srcFileName);

    static const char wspace[] = " \t\v\r\n";
    bool success = true;
    String mechanisms(auth.GetContent());
    size_t toks, toke;

    /*
     * Normalize the auth string to separate the tokens by a single space
     * rather than any random combination of white space characters.
     */
    toks = mechanisms.find_first_not_of(wspace);
    while (toks != String::npos) {
        toke = mechanisms.find_first_of(wspace, toks);
        authList += mechanisms.substr(toks, toke - toks);
        authList.push_back(' ');
        toks = mechanisms.find_first_not_of(wspace, toke);
    }
    if (!authList.empty()) {
        // Remove the trailing ' ' that was leftover from normalization.
        authList.erase(authList.size() - 1);
    }
    return success;
}


bool ConfigDB::DB::ProcessBusconfig(const String& srcFileName, const XmlElement& busconfig)
{
    bool success = true;
    const vector<XmlElement*>& elements = busconfig.GetChildren();
    vector<XmlElement*>::const_iterator it;

    for (it = elements.begin(); success && (it != elements.end()); ++it) {
        const String& tag = (*it)->GetName();
        if (tag == "auth") {
            success = ProcessAuth(srcFileName, *(*it));
        } else if (tag == "flag") {
            success = ProcessFlag(srcFileName, *(*it));
        } else if (tag == "fork") {
            success = ProcessFork(srcFileName, *(*it));
        } else if (tag == "include") {
            success = ProcessInclude(srcFileName, *(*it));
        } else if (tag == "includedir") {
            success = ProcessIncludedir(srcFileName, *(*it));
        } else if (tag == "keep_umask") {
            success = ProcessKeepUmask(srcFileName, *(*it));
        } else if (tag == "limit") {
            success = ProcessLimit(srcFileName, *(*it));
        } else if (tag == "listen") {
            success = ProcessListen(srcFileName, *(*it));
        } else if (tag == "pidfile") {
            success = ProcessPidfile(srcFileName, *(*it));
#ifdef ENABLE_POLICYDB
        } else if (tag == "policy") {
            success = ProcessPolicy(srcFileName, *(*it));
#else
        } else if (tag == "policy") {
            success = true;  // Ignore policies
#endif
        } else if (tag == "property") {
            success = ProcessProperty(srcFileName, *(*it));
        } else if (tag == "syslog") {
            success = ProcessSyslog(srcFileName, *(*it));
        } else if (tag == "type") {
            success = ProcessType(srcFileName, *(*it));
        } else if (tag == "user") {
            success = ProcessUser(srcFileName, *(*it));
        } else {
            Log(LOG_NOTICE, "Error processing \"%s\": Unknown tag found in <busconfig>: %s - ignoring\n",
                srcFileName.c_str(), tag.c_str());
        }
    }

    return success;
}


bool ConfigDB::DB::ProcessFork(const String& srcFileName, const XmlElement& forkElement)
{
    QCC_UNUSED(srcFileName);

    bool success = true;
    this->fork = true;

#ifndef NDEBUG
    if (!forkElement.GetAttributes().empty() || !forkElement.GetChildren().empty() || !forkElement.GetContent().empty()) {
        Log(LOG_INFO, "Ignoring extraneous data with <%s> tag.\n", forkElement.GetName().c_str());
    }
#else
    QCC_UNUSED(forkElement);
#endif

    return success;
}


bool ConfigDB::DB::ProcessInclude(const String& srcFileName, const XmlElement& include)
{
    bool success = true;
    String includeFileName = ExpandPath(include.GetContent(), srcFileName);
    const map<String, String>& attrs = include.GetAttributes();
    bool ignoreMissing = false;

    if (includeFileName.empty()) {
        success = false;
        Log(LOG_ERR, "Error processing \"%s\": <%s> block is empty.\n",
            srcFileName.c_str(), include.GetName().c_str());
        goto exit;
    }

    if (!attrs.empty()) {
        map<String, String>::const_iterator it;
        for (it = attrs.begin(); it != attrs.end(); ++it) {
            if (it->first == "ignore_missing") {
                ignoreMissing = (it->second == "yes");
            } else {
                Log(LOG_NOTICE, "Error Processing \"%s\": Unknown attribute \"%s\" in tag <%s> - ignoring.\n",
                    srcFileName.c_str(), it->first.c_str(), include.GetName().c_str());
            }
        }
    }

    success = ParseFile(includeFileName, ignoreMissing);

exit:
    return success;
}


bool ConfigDB::DB::ProcessIncludedir(const String& srcFileName, const XmlElement& includedir)
{
    bool success = true;
    String includeDirectory = ExpandPath(includedir.GetContent(), srcFileName);
    const map<String, String>& attrs = includedir.GetAttributes();
    bool ignoreMissing = false;
    DirListing listing;
    QStatus status;

    if (includeDirectory.empty()) {
        success = false;
        Log(LOG_ERR, "Error processing \"%s\": <%s> block is empty.\n",
            srcFileName.c_str(), includedir.GetName().c_str());
        goto exit;
    }

    if (!attrs.empty()) {
        map<String, String>::const_iterator it;
        for (it = attrs.begin(); it != attrs.end(); ++it) {
            if (it->first == "ignore_missing") {
                ignoreMissing = (it->second == "yes");
            } else {
                Log(LOG_NOTICE, "Error Processing \"%s\": Unknown attribute \"%s\" in tag <%s> - ignoring.\n",
                    srcFileName.c_str(), it->first.c_str(), includedir.GetName().c_str());
            }
        }
    }

    status = GetDirListing(includeDirectory.c_str(), listing);
    if (status != ER_OK) {
        if (!ignoreMissing) {
            Log(LOG_ERR, "Error processing \"%s\": Failed to access directory \"%s\": %s\n",
                srcFileName.c_str(), includeDirectory.c_str(), strerror(errno));
            success = false;
        }
        goto exit;
    }

    for (DirListing::const_iterator it = listing.begin(); it != listing.end(); ++it) {
        if (((*it).size() > 5) && ((*it).find(".conf", (*it).size() - 5))) {
            success = ParseFile(includeDirectory + "/" + *it);
        }
    }

exit:
    return success;
}


bool ConfigDB::DB::ProcessKeepUmask(const String& srcFileName, const XmlElement& keepUmaskElement)
{
    QCC_UNUSED(srcFileName);

    bool success = true;
    this->keepUmask = true;

#ifndef NDEBUG
    if (!keepUmaskElement.GetAttributes().empty() || !keepUmaskElement.GetChildren().empty() || !keepUmaskElement.GetContent().empty()) {
        Log(LOG_INFO, "Ignoring extraneous data with <%s> tag.\n", keepUmaskElement.GetName().c_str());
    }
#else
    QCC_UNUSED(keepUmaskElement);
#endif

    return success;
}


bool ConfigDB::DB::ProcessLimit(const String& srcFileName, const XmlElement& limit)
{
    bool success = true;
    String name = limit.GetAttribute("name");
    String valstr = limit.GetContent();
    uint32_t value;

    if (name.empty()) {
        Log(LOG_ERR, "Error processing \"%s\": 'name' attribute missing from <%s> tag.\n",
            srcFileName.c_str(), limit.GetName().c_str());
        success = false;
        goto exit;
    }

    if (valstr.empty()) {
        Log(LOG_ERR, "Error processing \"%s\": Value not specified for limit \"%s\".\n",
            srcFileName.c_str(), name.c_str());
        success = false;
        goto exit;
    }

    value = StringToU32(valstr);

    if ((value == 0) && (valstr[0] != '0')) {
        Log(LOG_ERR, "Error processing \"%s\": Limit value for \"%s\" must be an unsigned 32 bit integer (not \"%s\").\n",
            srcFileName.c_str(), name.c_str(), valstr.c_str());
        success = false;
        goto exit;
    }

    limitMap[name] = value;

exit:
    return success;
}


bool ConfigDB::DB::ProcessFlag(const String& srcFileName, const XmlElement& flag)
{
    bool success = true;
    String name = flag.GetAttribute("name");
    String valstr = flag.GetContent();

    if (name.empty()) {
        Log(LOG_ERR, "Error processing \"%s\": 'name' attribute missing from <%s> tag.\n",
            srcFileName.c_str(), flag.GetName().c_str());
        success = false;
        goto exit;
    }

    if (valstr == "true") {
        limitMap[name] = 1;
    } else if (valstr == "false") {
        limitMap[name] = 0;
    } else {
        Log(LOG_ERR, "Error processing \"%s\": Flag value for \"%s\" must be \"true\" or \"false\" (not \"%s\").\n",
            srcFileName.c_str(), name.c_str(), valstr.c_str());
        success = false;
    }

exit:
    return success;
}


bool ConfigDB::DB::ProcessProperty(const String& srcFileName, const XmlElement& property)
{
    bool success = true;
    String name = property.GetAttribute("name");
    String valstr = property.GetContent();

    if (name.empty()) {
        Log(LOG_ERR, "Error processing \"%s\": 'name' attribute missing from <%s> tag.\n",
            srcFileName.c_str(), property.GetName().c_str());
        success = false;
        goto exit;
    }

    propertyMap[name] = valstr;

exit:
    return success;
}


bool ConfigDB::DB::ProcessListen(const String& srcFileName, const XmlElement& listen)
{
    bool success = true;
    String addr = listen.GetContent();

    if (addr.empty()) {
        Log(LOG_ERR, "Error processing \"%s\": <%s> block is empty.\n",
            srcFileName.c_str(), listen.GetName().c_str());
        success = false;
    } else {
        bool added = listenList->insert(addr).second;
        if (!added) {
            Log(LOG_WARNING, "Warning processing \"%s\": Duplicate listen spec found (ignoring): %s\n",
                srcFileName.c_str(), addr.c_str());
        }
    }

    return success;
}


bool ConfigDB::DB::ProcessPidfile(const String& srcFileName, const XmlElement& pidfileElement)
{
    bool success = true;

    this->pidfile = ExpandPath(pidfileElement.GetContent(), srcFileName);
    if (this->pidfile.empty()) {
        success = false;
        Log(LOG_ERR, "Error processing \"%s\": <%s> block is empty.\n",
            srcFileName.c_str(), pidfileElement.GetName().c_str());
    }

    return success;
}


#ifdef ENABLE_POLICYDB
bool ConfigDB::DB::ProcessPolicy(const String& srcFileName, const XmlElement& policy)
{
    bool success = true;
    const vector<XmlElement*>& elements = policy.GetChildren();
    const map<String, String>& attrs = policy.GetAttributes();

    if (attrs.size() != 1) {
        Log(LOG_ERR, "Error processing \"%s\": Exactly one policy category must be specified.\n", srcFileName.c_str());
        return false;
    }

    map<String, String>::const_iterator pit = attrs.begin();
    const String& cat = pit->first;
    const String& catValue = pit->second;

    for (vector<XmlElement*>::const_iterator it = elements.begin(); success && (it != elements.end()); ++it) {
        const String& permStr = (*it)->GetName();
        QCC_DEBUG_ONLY(Log(LOG_DEBUG, "Processing tag <%s> in \"%s\"...\n", permStr.c_str(), srcFileName.c_str()));

        success = policyDB->AddRule(cat, catValue, permStr, (*it)->GetAttributes());

        if (!success) {
            Log(LOG_ERR, "Error processing \"%s\": Invalid policy: cat=\"%s\"  catValue=\"%s\" perm=\"%s\" \n",
                srcFileName.c_str(), cat.c_str(), catValue.c_str(), permStr.c_str());
        }
    }

    return success;
}
#endif

bool ConfigDB::DB::ProcessSyslog(const String& srcFileName, const XmlElement& syslogElement)
{
    QCC_UNUSED(srcFileName);

    bool success = true;
    this->syslog = true;

#ifndef NDEBUG
    if (!syslogElement.GetAttributes().empty() || !syslogElement.GetChildren().empty() || !syslogElement.GetContent().empty()) {
        Log(LOG_INFO, "Ignoring extraneous data with <syslog> tag.\n", syslogElement.GetName().c_str());
    }
#else
    QCC_UNUSED(syslogElement);
#endif

    return success;
}


bool ConfigDB::DB::ProcessType(const String& srcFileName, const XmlElement& typeElement)
{
    bool success = true;

    this->type = typeElement.GetContent();
    if (this->type.empty()) {
        success = false;
        Log(LOG_ERR, "Error processing \"%s\": <%s> block is empty.\n",
            srcFileName.c_str(), typeElement.GetName().c_str());
    }

    return success;
}


bool ConfigDB::DB::ProcessUser(const String& srcFileName, const XmlElement& userElement)
{
    bool success = true;

    this->user = userElement.GetContent();
    if (this->user.empty()) {
        success = false;
        Log(LOG_ERR, "Error processing \"%s\": <%s> block is empty.\n",
            srcFileName.c_str(), userElement.GetName().c_str());
    }

    return success;
}
