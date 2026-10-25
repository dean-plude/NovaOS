#!/usr/bin/env python3
"""Download a free Microsoft Store app's package and its framework
dependencies the way the Store client does it.

    python3 tools/appx/storefetch.py 9WZDNCRFHVN5 OUTDIR [--arch x64]

The route is the Store's own public delivery service (the same steps as
StoreDev/StoreLib, MIT, and the rg-adguard page):

  1. displaycatalog.mp.microsoft.com gives the product's WuCategoryId;
  2. fe3.delivery.mp.microsoft.com GetCookie, then SyncUpdates for that
     category, lists the app's bundle and every framework package it can
     depend on (all architectures and versions);
  3. GetExtendedUpdateInfo2 on the secured endpoint gives each file's
     signed, time-limited download URL on tlu.dl.delivery.mp.microsoft.com.

Nothing about the packages is touched: they keep Microsoft's signatures and
NovaOS's appx checks them on install like Windows does. Only free apps
download this way (an anonymous ticket; paid apps need a licence, which this
never works around).

fe3's TLS chain ends at "Microsoft Root Certificate Authority 2011", which is
in Windows' root store but not in the usual Mozilla bundle. The tool fetches
that root from Microsoft's PKI page and pins its SHA-1 thumbprint before
trusting it (--cacert takes a bundle instead).

Stdlib only.  The files land in OUTDIR named by package full name; a
manifest OUTDIR/store.json lists them (package full name, kind, size).
"""

import argparse
import datetime
import hashlib
import io
import json
import os
import re
import ssl
import sys
import tempfile
import urllib.request
import uuid
import xml.etree.ElementTree as ET
import zipfile

CATALOG = ("https://displaycatalog.mp.microsoft.com/v7.0/products/{}"
           "?market=US&languages=en-us&fieldsTemplate=Details")
FE3 = "https://fe3.delivery.mp.microsoft.com/ClientWebService/client.asmx"
ROOT_URL = ("https://www.microsoft.com/pki/certs/"
            "MicRooCerAut2011_2011_03_22.crt")
ROOT_SHA1 = "8f43288ad272f3103b6fb1428485ea3014c0bcfe"
ACTION = "http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService/"
WU = "{http://www.microsoft.com/SoftwareDistribution/Server/ClientWebService}"

# The non-leaf update ids every Store client reports as installed; without
# them SyncUpdates answers with nothing (list from StoreLib, MIT).
NONLEAF = (1, 2, 3, 11, 19, 544, 549, 2359974, 2359977, 5169044, 8788830,
           23110993, 23110994, 54341900, 54343656, 59830006, 59830007,
           59830008, 60484010, 62450018, 62450019, 62450020, 66027979,
           66053150, 97657898, 98822896, 98959022, 98959023, 98959024,
           98959025, 98959026, 104433538, 104900364, 105489019, 117765322,
           129905029, 130040031, 132387090, 132393049, 133399034, 138537048,
           140377312, 143747671, 158941041, 158941042, 158941043, 158941044,
           159123858, 159130928, 164836897, 164847386, 164848327, 164852241,
           164852246, 164852252, 164852253)

DEVICE = ("E:BranchReadinessLevel=CB&amp;CurrentBranch=ge_release&amp;"
          "FlightRing=Retail&amp;AttrDataVer=21&amp;InstallLanguage=en-US&amp;"
          "OSUILocale=en-US&amp;InstallationType=Client&amp;App=WU&amp;"
          "AppVer=10.0.26100.1&amp;OSArchitecture=AMD64&amp;"
          "UpdateManagementGroup=2&amp;IsFlightingEnabled=0&amp;"
          "TelemetryLevel=1&amp;OSVersion=10.0.26100.1&amp;"
          "DeviceFamily=Windows.Desktop")


def http(url, data=None, ctype=None, ctx=None):
    req = urllib.request.Request(url, data=data)
    if ctype:
        req.add_header("Content-Type", ctype)
    with urllib.request.urlopen(req, context=ctx, timeout=120) as r:
        return r.read()


def fe3_context(cacert):
    if cacert:
        return ssl.create_default_context(cafile=cacert)
    der = http(ROOT_URL, ctx=ssl.create_default_context())
    if hashlib.sha1(der).hexdigest() != ROOT_SHA1:
        sys.exit("storefetch: Microsoft root download has the wrong thumbprint")
    ctx = ssl.create_default_context()
    ctx.load_verify_locations(cadata=ssl.DER_cert_to_PEM_cert(der))
    return ctx


def envelope(action, body, url=FE3):
    now = datetime.datetime.now(datetime.timezone.utc)
    stamp = lambda t: t.strftime("%Y-%m-%dT%H:%M:%S.000Z")
    sec = "http://docs.oasis-open.org/wss/2004/01/"
    return f"""<s:Envelope xmlns:a="http://www.w3.org/2005/08/addressing" xmlns:s="http://www.w3.org/2003/05/soap-envelope">
<s:Header>
<a:Action s:mustUnderstand="1">{ACTION}{action}</a:Action>
<a:MessageID>urn:uuid:{uuid.uuid4()}</a:MessageID>
<a:To s:mustUnderstand="1">{url}</a:To>
<o:Security s:mustUnderstand="1" xmlns:o="{sec}oasis-200401-wss-wssecurity-secext-1.0.xsd">
<Timestamp xmlns="{sec}oasis-200401-wss-wssecurity-utility-1.0.xsd"><Created>{stamp(now)}</Created><Expires>{stamp(now + datetime.timedelta(minutes=5))}</Expires></Timestamp>
<wuws:WindowsUpdateTicketsToken wsu:id="ClientMSA" xmlns:wsu="{sec}oasis-200401-wss-wssecurity-utility-1.0.xsd" xmlns:wuws="http://schemas.microsoft.com/msus/2014/10/WindowsUpdateAuthorization"><TicketType Name="AAD" Version="1.0" Policy="MBI_SSL"></TicketType></wuws:WindowsUpdateTicketsToken>
</o:Security>
</s:Header>
<s:Body>{body}</s:Body>
</s:Envelope>""".encode()


def soap(ctx, action, body, url=FE3):
    raw = http(url, envelope(action, body, url),
               "application/soap+xml; charset=utf-8", ctx)
    return ET.fromstring(raw)


def category_id(product):
    d = json.loads(http(CATALOG.format(product)))
    for sku in d["Product"]["DisplaySkuAvailabilities"]:
        fd = sku["Sku"]["Properties"].get("FulfillmentData")
        if fd and fd.get("WuCategoryId"):
            return fd["WuCategoryId"], fd.get("PackageFamilyName", "")
    sys.exit(f"storefetch: {product} has no Store package (not a packaged app)")


def cookie(ctx):
    body = (f'<GetCookie xmlns="{WU[1:-1]}"><oldCookie></oldCookie>'
            '<lastChange>2015-10-21T17:01:07.1472913Z</lastChange>'
            '<currentTime>2015-10-21T17:01:07.1472913Z</currentTime>'
            '<protocolVersion>1.40</protocolVersion></GetCookie>')
    r = soap(ctx, "GetCookie", body)
    return r.find(f".//{WU}EncryptedData").text


def sync(ctx, enc, category):
    ids = "".join(f"<int>{i}</int>" for i in NONLEAF)
    body = f"""<SyncUpdates xmlns="{WU[1:-1]}"><cookie><Expiration>2045-03-11T02:02:48Z</Expiration><EncryptedData>{enc}</EncryptedData></cookie><parameters><ExpressQuery>false</ExpressQuery><InstalledNonLeafUpdateIDs>{ids}</InstalledNonLeafUpdateIDs><OtherCachedUpdateIDs></OtherCachedUpdateIDs><SkipSoftwareSync>false</SkipSoftwareSync><NeedTwoGroupOutOfScopeUpdates>true</NeedTwoGroupOutOfScopeUpdates><FilterAppCategoryIds><CategoryIdentifier><Id>{category}</Id></CategoryIdentifier></FilterAppCategoryIds><TreatAppCategoryIdsAsInstalled>true</TreatAppCategoryIdsAsInstalled><AlsoPerformRegularSync>false</AlsoPerformRegularSync><ComputerSpec/><ExtendedUpdateInfoParameters><XmlUpdateFragmentTypes><XmlUpdateFragmentType>Extended</XmlUpdateFragmentType></XmlUpdateFragmentTypes><Locales><string>en-US</string><string>en</string></Locales></ExtendedUpdateInfoParameters><ClientPreferredLanguages><string>en-US</string></ClientPreferredLanguages><ProductsParameters><SyncCurrentVersionOnly>false</SyncCurrentVersionOnly><DeviceAttributes>{DEVICE}</DeviceAttributes><CallerAttributes>E:Interactive=1&amp;IsSeeker=1&amp;Acquisition=1&amp;SheduledInstall=0&amp;IsWuApp=1&amp;Id=MoAppStore&amp;</CallerAttributes><Products/></ProductsParameters></parameters></SyncUpdates>"""
    r = soap(ctx, "SyncUpdates", body)
    # Each extended update carries the file (name + package full name) under
    # an update number; each new update maps that number to the identity
    # GetExtendedUpdateInfo2 wants. Only identities with a SecuredFragment
    # are the downloadable packages.
    files = {}
    for u in r.iter(f"{WU}Update"):
        num, xml = u.find(f"{WU}ID").text, u.find(f"{WU}Xml").text or ""
        frag = ET.fromstring(f"<x>{xml}</x>")
        for f in frag.iter("File"):
            full = f.get("InstallerSpecificIdentifier")
            if full:
                files[num] = (full, f.get("FileName"), int(f.get("Size", 0)))
    out = []
    for u in r.iter(f"{WU}UpdateInfo"):
        num, xml = u.find(f"{WU}ID").text, u.find(f"{WU}Xml").text or ""
        if "SecuredFragment" not in xml or num not in files:
            continue
        ident = ET.fromstring(f"<x>{xml}</x>").find(".//UpdateIdentity")
        out.append(files[num] + (ident.get("UpdateID"),
                                 ident.get("RevisionNumber")))
    return out


def file_url(ctx, update_id, rev, name):
    body = f"""<GetExtendedUpdateInfo2 xmlns="{WU[1:-1]}"><updateIDs><UpdateIdentity><UpdateID>{update_id}</UpdateID><RevisionNumber>{rev}</RevisionNumber></UpdateIdentity></updateIDs><infoTypes><XmlUpdateFragmentType>FileUrl</XmlUpdateFragmentType><XmlUpdateFragmentType>FileDecryption</XmlUpdateFragmentType></infoTypes><deviceAttributes>{DEVICE}</deviceAttributes></GetExtendedUpdateInfo2>"""
    r = soap(ctx, "GetExtendedUpdateInfo2", body, FE3 + "/secured")
    urls = [loc.find(f"{WU}Url").text for loc in r.iter(f"{WU}FileLocation")]
    # The answer also lists the block map; the package's own URL is the
    # long one that is not a .BlockMap.
    urls = [u for u in urls if "blockmap" not in u.lower()]
    if not urls:
        sys.exit(f"storefetch: no download URL for {name}")
    return max(urls, key=len)


def parse_full(full):
    # Name_Version_Arch_ResourceId_PublisherId
    name, ver, arch, _res, pub = full.split("_")
    return name, tuple(int(x) for x in ver.split(".")), arch, pub


def choose(files, family, arch):
    """The newest app bundle of FAMILY, plus the newest package of each
    framework name for ARCH (Windows picks the same way)."""
    pick = {}
    for full, fname, size, uid, rev in files:
        name, ver, a, pub = parse_full(full)
        if f"{name}_{pub}" == family:
            key, ok = "app", fname.lower().endswith(("bundle", ".msix", ".appx"))
        else:
            key, ok = name, a in (arch, "neutral")
        if not ok or fname.rsplit(".", 1)[-1].lower().startswith("e"):
            continue  # .eappx/.emsixbundle are encrypted (paid-app) copies
        if key not in pick or ver > pick[key][1]:
            pick[key] = (full, ver, fname, size, uid, rev)
    return pick


def dependencies(path, arch):
    """PackageDependency names of the ARCH (or neutral) app package inside
    a bundle, or of a plain package."""
    with zipfile.ZipFile(path) as z:
        names = z.namelist()
        if "AppxManifest.xml" not in names:
            inner = [n for n in names if n.lower().endswith((".msix", ".appx"))
                     and f"_{arch}." in n.lower()]
            if not inner:
                sys.exit(f"storefetch: {path} has no {arch} package")
            with zipfile.ZipFile(io.BytesIO(z.read(inner[0]))) as i:
                xml = i.read("AppxManifest.xml")
        else:
            xml = z.read("AppxManifest.xml")
    return [e.get("Name") for e in ET.fromstring(xml).iter()
            if e.tag.endswith("}PackageDependency")]


def fetch(ctx, out, entry, kind):
    full, _v, fname, size, uid, rev = entry
    # The Store names files by GUID; keep the package full name instead.
    dest = os.path.join(out, full + "." + fname.rsplit(".", 1)[-1])
    if not (os.path.exists(dest) and os.path.getsize(dest) == size):
        url = file_url(ctx, uid, rev, fname)
        print(f"storefetch: {os.path.basename(dest)} ({size // 1024} KiB)")
        fd, tmp = tempfile.mkstemp(dir=out)
        with os.fdopen(fd, "wb") as w, urllib.request.urlopen(
                url, timeout=600) as r:
            while chunk := r.read(1 << 20):
                w.write(chunk)
        os.replace(tmp, dest)
    return {"full_name": full, "file": os.path.basename(dest), "size": size,
            "kind": kind}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("product", help="Store product id, e.g. 9WZDNCRFHVN5")
    ap.add_argument("out", help="output folder")
    ap.add_argument("--arch", default="x64")
    ap.add_argument("--cacert", help="CA bundle for fe3 (default: pinned "
                    "Microsoft Root Certificate Authority 2011)")
    ap.add_argument("--list", action="store_true",
                    help="print every file the Store offers and stop")
    a = ap.parse_args()

    category, family = category_id(a.product)
    ctx = fe3_context(a.cacert)
    files = sync(ctx, cookie(ctx), category)
    if a.list:
        for f in sorted(files):
            print(f[0], f[1], f[2])
        return
    pick = choose(files, family, a.arch)
    if "app" not in pick:
        sys.exit(f"storefetch: the Store offered no package of {family}")
    os.makedirs(a.out, exist_ok=True)
    manifest = [fetch(ctx, a.out, pick["app"], "app")]
    # The Store lists every framework any version ever used; take only the
    # ones the app's own manifest depends on.
    for dep in dependencies(os.path.join(a.out, manifest[0]["file"]), a.arch):
        if dep not in pick:
            sys.exit(f"storefetch: the Store offered no {dep} for {a.arch}")
        manifest.append(fetch(ctx, a.out, pick[dep], "framework"))
    with open(os.path.join(a.out, "store.json"), "w") as f:
        json.dump({"product": a.product, "family": family,
                   "packages": manifest}, f, indent=1)
    print(f"storefetch: {len(manifest)} packages in {a.out}")


if __name__ == "__main__":
    main()
