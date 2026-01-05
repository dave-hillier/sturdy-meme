/*
 * lime-init/02-coogee-framework.js
 * Part 2/8: Coogee UI Framework
 * Contains: com.watabou.coogee.*, com.watabou.formats.*
 */
                },
                __class__: sb
            });
            var rc = function(a) {
                a.addChild(this);
                sb.call(this);
                this.dispatchEvent(new wa("addedToStage", !1, !1))
            };
            g.DocumentClass = rc;
            rc.__name__ = "DocumentClass";
            rc.__super__ = sb;
            rc.prototype = v(sb.prototype, {
                __class__: rc
            });
            var ja = function(a, b) {
                this.r = new RegExp(a, b.split("u").join(""))
            };
            g.EReg = ja;
            ja.__name__ = "EReg";
            ja.prototype = {
                match: function(a) {
                    this.r.global && (this.r.lastIndex = 0);
                    this.r.m = this.r.exec(a);
                    this.r.s = a;
                    return null != this.r.m
                },
                matched: function(a) {
                    if (null != this.r.m && 0 <= a && a < this.r.m.length) return this.r.m[a];
                    throw X.thrown("EReg::matched");
                },
                matchedPos: function() {
                    if (null == this.r.m) throw X.thrown("No string matched");
                    return {
                        pos: this.r.m.index,
                        len: this.r.m[0].length
                    }
                },
                matchSub: function(a, b, c) {
                    null == c && (c = -1);
                    if (this.r.global) {
                        if (this.r.lastIndex = b, this.r.m = this.r.exec(0 > c ? a : N.substr(a, 0, b + c)), c = null != this.r.m) this.r.s = a
                    } else if (c = this.match(0 > c ? N.substr(a, b, null) : N.substr(a, b, c))) this.r.s = a, this.r.m.index += b;
                    return c
                },
                split: function(a) {
                    return a.replace(this.r, "#__delim__#").split("#__delim__#")
                },
                map: function(a, b) {
                    var c = 0,
                        d = "";
                    do {
                        if (c >= a.length) break;
                        else if (!this.matchSub(a, c)) {
                            d += H.string(N.substr(a, c, null));
                            break
                        }
                        var f = this.matchedPos();
                        d += H.string(N.substr(a,
                            c, f.pos - c));
                        d += H.string(b(this));
                        0 == f.len ? (d += H.string(N.substr(a, f.pos, 1)), c = f.pos + 1) : c = f.pos + f.len
                    } while (this.r.global);
                    !this.r.global && 0 < c && c < a.length && (d += H.string(N.substr(a, c, null)));
                    return d
                },
                __class__: ja
            };
            var N = function() {};
            g.HxOverrides = N;
            N.__name__ = "HxOverrides";
            N.strDate = function(a) {
                switch (a.length) {
                    case 8:
                        a = a.split(":");
                        var b = new Date;
                        b.setTime(0);
                        b.setUTCHours(a[0]);
                        b.setUTCMinutes(a[1]);
                        b.setUTCSeconds(a[2]);
                        return b;
                    case 10:
                        return a = a.split("-"), new Date(a[0], a[1] - 1, a[2], 0, 0, 0);
                    case 19:
                        return a =
                            a.split(" "), b = a[0].split("-"), a = a[1].split(":"), new Date(b[0], b[1] - 1, b[2], a[0], a[1], a[2]);
                    default:
                        throw X.thrown("Invalid date format : " + a);
                }
            };
            N.cca = function(a, b) {
                a = a.charCodeAt(b);
                if (a == a) return a
            };
            N.substr = function(a, b, c) {
                if (null == c) c = a.length;
                else if (0 > c)
                    if (0 == b) c = a.length + c;
                    else return "";
                return a.substr(b, c)
            };
            N.remove = function(a, b) {
                b = a.indexOf(b);
                if (-1 == b) return !1;
                a.splice(b, 1);
                return !0
            };
            N.now = function() {
                return Date.now()
            };
            var F = function() {};
            g.Lambda = F;
            F.__name__ = "Lambda";
            F.array = function(a) {
                var b = [];
                for (a = L(a); a.hasNext();) {
                    var c = a.next();
                    b.push(c)
                }
                return b
            };
            var Ab = function() {};
            g.ManifestResources = Ab;
            Ab.__name__ = "ManifestResources";
            Ab.init = function(a) {
                Ab.preloadLibraries = [];
                Ab.preloadLibraryNames = [];
                Ab.rootPath = null;
                null != a && Object.prototype.hasOwnProperty.call(a, "rootPath") && (Ab.rootPath = ya.field(a, "rootPath"), O.endsWith(Ab.rootPath, "/") || (Ab.rootPath += "/"));
                null == Ab.rootPath && (Ab.rootPath = "./");
                ha.registerFont(ca);
                ha.registerFont(T);
                ha.registerFont(cd);
                a = td.parse('{"name":null,"assets":"aoy4:sizei212148y4:typey4:FONTy9:classNamey45:__ASSET__assets_imfellgreatprimer_regular_ttfy2:idy12:default_fonty7:preloadtgoR0i46596R1R2R3y37:__ASSET__assets_sharetech_regular_ttfR5y7:ui_fontR7tgoR0i42756R1R2R3y41:__ASSET__assets_sharetechmono_regular_ttfR5y12:ui_font_monoR7tgoy4:pathy21:Assets%2Fdefault.jsonR0i343R1y4:TEXTR5y7:defaultR7tgoR12y17:Assets%2Fink.jsonR0i343R1R14R5y3:inkR7tgoR12y16:Assets%2Fbw.jsonR0i341R1R14R5y2:bwR7tgoR12y19:Assets%2Fvivid.jsonR0i341R1R14R5y5:vividR7tgoR12y21:Assets%2Fnatural.jsonR0i341R1R14R5y7:naturalR7tgoR12y20:Assets%2Fmodern.jsonR0i340R1R14R5y6:modernR7tgoR12y21:Assets%2Fgrammar.jsonR0i4401R1R14R5y7:grammarR7tgoR12y20:Assets%2Fenglish.txtR0i6166R1R14R5y7:englishR7tgoR12y18:Assets%2Felven.txtR0i332R1R14R5y5:elvenR7tgoR12y23:Assets%2Fgiven_male.txtR0i808R1R14R5y4:maleR7tgoR12y25:Assets%2Fgiven_female.txtR0i665R1R14R5y6:femaleR7tgh","rootPath":null,"version":2,"libraryArgs":[],"libraryType":null}',
                    Ab.rootPath);
                a = Wb.fromManifest(a);
                Fa.registerLibrary("default", a);
                a = Fa.getLibrary("default");
                null != a ? Ab.preloadLibraries.push(a) : Ab.preloadLibraryNames.push("default")
            };
            var ib = function(a) {
                null != a && (this.name = a);
                this.__init || (void 0 == this.ascender && (this.ascender = 0), void 0 == this.descender && (this.descender = 0), void 0 == this.height && (this.height = 0), void 0 == this.numGlyphs && (this.numGlyphs = 0), void 0 == this.underlinePosition && (this.underlinePosition = 0), void 0 == this.underlineThickness && (this.underlineThickness =
                    0), void 0 == this.unitsPerEM && (this.unitsPerEM = 0), null != this.__fontID ? Fa.isLocal(this.__fontID) && this.__fromBytes(Fa.getBytes(this.__fontID)) : null != this.__fontPath && this.__fromFile(this.__fontPath))
            };
            g["lime.text.Font"] = ib;
            ib.__name__ = "lime.text.Font";
            ib.fromFile = function(a) {
                if (null == a) return null;
                var b = new ib;
                b.__fromFile(a);
                return b
            };
            ib.loadFromName = function(a) {
                return (new ib).__loadFromName(a)
            };
            ib.__measureFontNode = function(a) {
                var b = window.document.createElement("span");
                b.setAttribute("aria-hidden",
                    "true");
                var c = window.document.createTextNode("BESbswy");
                b.appendChild(c);
                c = b.style;
                c.display = "block";
                c.position = "absolute";
                c.top = "-9999px";
                c.left = "-9999px";
                c.fontSize = "300px";
                c.width = "auto";
                c.height = "auto";
                c.lineHeight = "normal";
                c.margin = "0";
                c.padding = "0";
                c.fontVariant = "normal";
                c.whiteSpace = "nowrap";
                c.fontFamily = a;
                window.document.body.appendChild(b);
                return b
            };
            ib.prototype = {
                __copyFrom: function(a) {
                    null != a && (this.ascender = a.ascender, this.descender = a.descender, this.height = a.height, this.name = a.name, this.numGlyphs =
                        a.numGlyphs, this.src = a.src, this.underlinePosition = a.underlinePosition, this.underlineThickness = a.underlineThickness, this.unitsPerEM = a.unitsPerEM, this.__fontID = a.__fontID, this.__fontPath = a.__fontPath, this.__init = !0)
                },
                __fromBytes: function(a) {
                    this.__fontPath = null
                },
                __fromFile: function(a) {
                    this.__fontPath = a
                },
                __loadFromName: function(a) {
                    var b = this,
                        c = new Gd;
                    this.name = a;
                    var d = E.navigator.userAgent.toLowerCase(),
                        f = 0 <= d.indexOf(" safari/") && 0 > d.indexOf(" chrome/");
                    d = (new ja("(iPhone|iPod|iPad).*AppleWebKit(?!.*Version)",
                        "i")).match(d);
                    if (!f && !d && window.document.fonts && (G = window.document.fonts, l(G, G.load))) window.document.fonts.load("1em '" + a + "'").then(function(a) {
                        c.complete(b)
                    }, function(d) {
                        Ga.warn('Could not load web font "' + a + '"', {
                            fileName: "lime/text/Font.hx",
                            lineNumber: 640,
                            className: "lime.text.Font",
                            methodName: "__loadFromName"
                        });
                        c.complete(b)
                    });
                    else {
                        var h = ib.__measureFontNode("'" + a + "', sans-serif"),
                            k = ib.__measureFontNode("'" + a + "', serif"),
                            n = h.offsetWidth,
                            p = k.offsetWidth,
                            P = -1,
                            g = 0,
                            m, q;
                        P = window.setInterval(function() {
                            g +=
                                1;
                            m = h.offsetWidth != n || k.offsetWidth != p;
                            q = 3E3 <= 50 * g;
                            if (m || q) window.clearInterval(P), h.parentNode.removeChild(h), k.parentNode.removeChild(k), k = h = null, q && Ga.warn('Could not load web font "' + a + '"', {
                                fileName: "lime/text/Font.hx",
                                lineNumber: 675,
                                className: "lime.text.Font",
                                methodName: "__loadFromName"
                            }), c.complete(b)
                        }, 50)
                    }
                    return c.future
                },
                __class__: ib
            };
            var Aa = t.__ASSET__assets_imfellgreatprimer_regular_ttf = function() {
                this.ascender = 1942;
                this.descender = -562;
                this.height = 2504;
                this.numGlyphs = 369;
                this.underlinePosition = -200;
                this.underlineThickness = 84;
                this.unitsPerEM = 2048;
                this.name = "IM FELL Great Primer Roman";
                ib.call(this)
            };
            g.__ASSET__assets_imfellgreatprimer_regular_ttf = Aa;
            Aa.__name__ = "__ASSET__assets_imfellgreatprimer_regular_ttf";
            Aa.__super__ = ib;
            Aa.prototype = v(ib.prototype, {
                __class__: Aa
            });
            var Ia = t.__ASSET__assets_sharetech_regular_ttf = function() {
                this.ascender = 885;
                this.descender = -242;
                this.height = 1127;
                this.numGlyphs = 256;
                this.underlinePosition = -100;
                this.underlineThickness = 50;
                this.unitsPerEM = 1E3;
                this.name = "Share Tech Regular";
                ib.call(this)
            };
            g.__ASSET__assets_sharetech_regular_ttf = Ia;
            Ia.__name__ = "__ASSET__assets_sharetech_regular_ttf";
            Ia.__super__ = ib;
            Ia.prototype = v(ib.prototype, {
                __class__: Ia
            });
            var Na = t.__ASSET__assets_sharetechmono_regular_ttf = function() {
                this.ascender = 885;
                this.descender = -242;
                this.height = 1127;
                this.numGlyphs = 268;
                this.underlinePosition = -135;
                this.underlineThickness = 50;
                this.unitsPerEM = 1E3;
                this.name = "Share Tech Mono";
                ib.call(this)
            };
            g.__ASSET__assets_sharetechmono_regular_ttf = Na;
            Na.__name__ = "__ASSET__assets_sharetechmono_regular_ttf";
            Na.__super__ = ib;
            Na.prototype = v(ib.prototype, {
                __class__: Na
            });
            var ha = function(a) {
                ib.call(this, a)
            };
            g["openfl.text.Font"] = ha;
            ha.__name__ = "openfl.text.Font";
            ha.fromFile = function(a) {
                if (null == a) return null;
                var b = new ha;
                b.__fromFile(a);
                return b
            };
            ha.loadFromName = function(a) {
                return ib.loadFromName(a).then(function(a) {
                    var b = new ha;
                    b.__fromLimeFont(a);
                    return hc.withValue(b)
                })
            };
            ha.registerFont = function(a) {
                a = null == va.getClass(a) ? va.__cast(w.createInstance(a, []), ha) : va.__cast(a, ha);
                null != a && (ha.__registeredFonts.push(a),
                    ha.__fontByName.h[a.name] = a)
            };
            ha.__super__ = ib;
            ha.prototype = v(ib.prototype, {
                __fromLimeFont: function(a) {
                    this.__copyFrom(a)
                },
                __class__: ha
            });
            var ca = t.__ASSET__OPENFL__assets_imfellgreatprimer_regular_ttf = function() {
                this.__fromLimeFont(new Aa);
                ib.call(this, void 0)
            };
            g.__ASSET__OPENFL__assets_imfellgreatprimer_regular_ttf = ca;
            ca.__name__ = "__ASSET__OPENFL__assets_imfellgreatprimer_regular_ttf";
            ca.__super__ = ha;
            ca.prototype = v(ha.prototype, {
                __class__: ca
            });
            var T = t.__ASSET__OPENFL__assets_sharetech_regular_ttf =
                function() {
                    this.__fromLimeFont(new Ia);
                    ib.call(this, void 0)
                };
            g.__ASSET__OPENFL__assets_sharetech_regular_ttf = T;
            T.__name__ = "__ASSET__OPENFL__assets_sharetech_regular_ttf";
            T.__super__ = ha;
            T.prototype = v(ha.prototype, {
                __class__: T
            });
            var cd = t.__ASSET__OPENFL__assets_sharetechmono_regular_ttf = function() {
                this.__fromLimeFont(new Na);
                ib.call(this, void 0)
            };
            g.__ASSET__OPENFL__assets_sharetechmono_regular_ttf = cd;
            cd.__name__ = "__ASSET__OPENFL__assets_sharetechmono_regular_ttf";
            cd.__super__ = ha;
            cd.prototype = v(ha.prototype, {
                __class__: cd
            });
            Math.__name__ = "Math";
            var ya = function() {};
            g.Reflect = ya;
            ya.__name__ = "Reflect";
            ya.field = function(a, b) {
                try {
                    return a[b]
                } catch (c) {
                    return Ta.lastError = c, null
                }
            };
            ya.getProperty = function(a, b) {
                var c;
                if (null == a) return null;
                var d = a.__properties__ ? c = a.__properties__["get_" + b] : !1;
                return d ? a[c]() : a[b]
            };
            ya.fields = function(a) {
                var b = [];
                if (null != a) {
                    var c = Object.prototype.hasOwnProperty,
                        d;
                    for (d in a) "__id__" != d && "hx__closures__" != d && c.call(a, d) && b.push(d)
                }
                return b
            };
            ya.isFunction = function(a) {
                return "function" ==
                    typeof a ? !(a.__name__ || a.__ename__) : !1
            };
            ya.compare = function(a, b) {
                return a == b ? 0 : a > b ? 1 : -1
            };
            ya.deleteField = function(a, b) {
                if (!Object.prototype.hasOwnProperty.call(a, b)) return !1;
                delete a[b];
                return !0
            };
            var H = function() {};
            g.Std = H;
            H.__name__ = "Std";
            H.string = function(a) {
                return va.__string_rec(a, "")
            };
            H.parseInt = function(a) {
                a = parseInt(a);
                return isNaN(a) ? null : a
            };
            var wb = function() {};
            g["_String.String_Impl_"] = wb;
            wb.__name__ = "_String.String_Impl_";
            wb.fromCharCode = function(a) {
                return String.fromCodePoint(a)
            };
            var x =
                function() {
                    this.b = ""
                };
            g.StringBuf = x;
            x.__name__ = "StringBuf";
            x.prototype = {
                __class__: x
            };
            var O = function() {};
            g.StringTools = O;
            O.__name__ = "StringTools";
            O.htmlEscape = function(a, b) {
                for (var c = "", d = 0, f = a; d < f.length;) {
                    a = f;
                    var h = d++,
                        k = a.charCodeAt(h);
                    55296 <= k && 56319 >= k && (k = k - 55232 << 10 | a.charCodeAt(h + 1) & 1023);
                    a = k;
                    65536 <= a && ++d;
                    switch (a) {
                        case 34:
                            c = b ? c + "&quot;" : c + String.fromCodePoint(a);
                            break;
                        case 38:
                            c += "&amp;";
                            break;
                        case 39:
                            c = b ? c + "&#039;" : c + String.fromCodePoint(a);
                            break;
                        case 60:
                            c += "&lt;";
                            break;
                        case 62:
                            c += "&gt;";
                            break;
                        default:
                            c += String.fromCodePoint(a)
                    }
                }
                return c
            };
            O.htmlUnescape = function(a) {
                return a.split("&gt;").join(">").split("&lt;").join("<").split("&quot;").join('"').split("&#039;").join("'").split("&amp;").join("&")
            };
            O.startsWith = function(a, b) {
                return a.length >= b.length ? 0 == a.lastIndexOf(b, 0) : !1
            };
            O.endsWith = function(a, b) {
                var c = b.length,
                    d = a.length;
                return d >= c ? a.indexOf(b, d - c) == d - c : !1
            };
            O.isSpace = function(a, b) {
                a = N.cca(a, b);
                return 8 < a && 14 > a ? !0 : 32 == a
            };
            O.ltrim = function(a) {
                for (var b = a.length, c = 0; c < b && O.isSpace(a,
                        c);) ++c;
                return 0 < c ? N.substr(a, c, b - c) : a
            };
            O.rtrim = function(a) {
                for (var b = a.length, c = 0; c < b && O.isSpace(a, b - c - 1);) ++c;
                return 0 < c ? N.substr(a, 0, b - c) : a
            };
            O.trim = function(a) {
                return O.ltrim(O.rtrim(a))
            };
            O.replace = function(a, b, c) {
                return a.split(b).join(c)
            };
            O.hex = function(a, b) {
                var c = "";
                do c = "0123456789ABCDEF".charAt(a & 15) + c, a >>>= 4; while (0 < a);
                if (null != b)
                    for (; c.length < b;) c = "0" + c;
                return c
            };
            var J = y.ValueType = {
                __ename__: "ValueType",
                __constructs__: null,
                TNull: {
                    _hx_name: "TNull",
                    _hx_index: 0,
                    __enum__: "ValueType",
                    toString: r
                },
                TInt: {
                    _hx_name: "TInt",
                    _hx_index: 1,
                    __enum__: "ValueType",
                    toString: r
                },
                TFloat: {
                    _hx_name: "TFloat",
                    _hx_index: 2,
                    __enum__: "ValueType",
                    toString: r
                },
                TBool: {
                    _hx_name: "TBool",
                    _hx_index: 3,
                    __enum__: "ValueType",
                    toString: r
                },
                TObject: {
                    _hx_name: "TObject",
                    _hx_index: 4,
                    __enum__: "ValueType",
                    toString: r
                },
                TFunction: {
                    _hx_name: "TFunction",
                    _hx_index: 5,
                    __enum__: "ValueType",
                    toString: r
                },
                TClass: (G = function(a) {
                    return {
                        _hx_index: 6,
                        c: a,
                        __enum__: "ValueType",
                        toString: r
                    }
                }, G._hx_name = "TClass", G.__params__ = ["c"], G),
                TEnum: (G = function(a) {
                    return {
                        _hx_index: 7,
                        e: a,
                        __enum__: "ValueType",
                        toString: r
                    }
                }, G._hx_name = "TEnum", G.__params__ = ["e"], G),
                TUnknown: {
                    _hx_name: "TUnknown",
                    _hx_index: 8,
                    __enum__: "ValueType",
                    toString: r
                }
            };
            J.__constructs__ = [J.TNull, J.TInt, J.TFloat, J.TBool, J.TObject, J.TFunction, J.TClass, J.TEnum, J.TUnknown];
            var w = function() {};
            g.Type = w;
            w.__name__ = "Type";
            w.resolveEnum = function(a) {
                return y[a]
            };
            w.createInstance = function(a, b) {
                return new(Function.prototype.bind.apply(a, [null].concat(b)))
            };
            w.createEnum = function(a, b, c) {
                var d = ya.field(a, b);
                if (null == d) throw X.thrown("No such constructor " +
                    b);
                if (ya.isFunction(d)) {
                    if (null == c) throw X.thrown("Constructor " + b + " need parameters");
                    return d.apply(a, c)
                }
                if (null != c && 0 != c.length) throw X.thrown("Constructor " + b + " does not need parameters");
                return d
            };
            w.typeof = function(a) {
                switch (typeof a) {
                    case "boolean":
                        return J.TBool;
                    case "function":
                        return a.__name__ || a.__ename__ ? J.TObject : J.TFunction;
                    case "number":
                        return Math.ceil(a) == a % 2147483648 ? J.TInt : J.TFloat;
                    case "object":
                        if (null == a) return J.TNull;
                        var b = a.__enum__;
                        if (null != b) return J.TEnum(y[b]);
                        a = va.getClass(a);
                        return null != a ? J.TClass(a) : J.TObject;
                    case "string":
                        return J.TClass(String);
                    case "undefined":
                        return J.TNull;
                    default:
                        return J.TUnknown
                }
            };
            w.enumParameters = function(a) {
                var b = y[a.__enum__].__constructs__[a._hx_index].__params__;
                if (null != b) {
                    for (var c = [], d = 0; d < b.length;) {
                        var f = b[d];
                        ++d;
                        c.push(a[f])
                    }
                    return c
                }
                return []
            };
            var cb = {
                    gt: function(a, b) {
                        var c = 0 > a;
                        return c != 0 > b ? c : a > b
                    },
                    toFloat: function(a) {
                        return 0 > a ? 4294967296 + a : a + 0
                    }
                },
                Cb = {
                    toString: function(a) {
                        switch (a) {
                            case 0:
                                return "Element";
                            case 1:
                                return "PCData";
                            case 2:
                                return "CData";
                            case 3:
                                return "Comment";
                            case 4:
                                return "DocType";
                            case 5:
                                return "ProcessingInstruction";
                            case 6:
                                return "Document"
                        }
                    }
                },
                W = function(a) {
                    this.nodeType = a;
                    this.children = [];
                    this.attributeMap = new Qa
                };
            g.Xml = W;
            W.__name__ = "Xml";
            W.parse = function(a) {
                return ef.parse(a)
            };
            W.createElement = function(a) {
                var b = new W(W.Element);
                if (b.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == b.nodeType ? "null" : Cb.toString(b.nodeType)));
                b.nodeName = a;
                return b
            };
            W.createPCData = function(a) {
                var b = new W(W.PCData);
                if (b.nodeType == W.Document || b.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == b.nodeType ? "null" : Cb.toString(b.nodeType)));
                b.nodeValue = a;
                return b
            };
            W.createCData = function(a) {
                var b = new W(W.CData);
                if (b.nodeType == W.Document || b.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == b.nodeType ? "null" : Cb.toString(b.nodeType)));
                b.nodeValue = a;
                return b
            };
            W.createComment = function(a) {
                var b = new W(W.Comment);
                if (b.nodeType == W.Document || b.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " +
                    (null == b.nodeType ? "null" : Cb.toString(b.nodeType)));
                b.nodeValue = a;
                return b
            };
            W.createDocType = function(a) {
                var b = new W(W.DocType);
                if (b.nodeType == W.Document || b.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == b.nodeType ? "null" : Cb.toString(b.nodeType)));
                b.nodeValue = a;
                return b
            };
            W.createProcessingInstruction = function(a) {
                var b = new W(W.ProcessingInstruction);
                if (b.nodeType == W.Document || b.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == b.nodeType ? "null" : Cb.toString(b.nodeType)));
                b.nodeValue = a;
                return b
            };
            W.createDocument = function() {
                return new W(W.Document)
            };
            W.prototype = {
                get: function(a) {
                    if (this.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    return this.attributeMap.h[a]
                },
                set: function(a, b) {
                    if (this.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    this.attributeMap.h[a] = b
                },
                remove: function(a) {
                    if (this.nodeType !=
                        W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    var b = this.attributeMap;
                    Object.prototype.hasOwnProperty.call(b.h, a) && delete b.h[a]
                },
                exists: function(a) {
                    if (this.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    return Object.prototype.hasOwnProperty.call(this.attributeMap.h, a)
                },
                attributes: function() {
                    if (this.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " +
                        (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    return new Ph(this.attributeMap.h)
                },
                firstElement: function() {
                    if (this.nodeType != W.Document && this.nodeType != W.Element) throw X.thrown("Bad node type, expected Element or Document but found " + (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    for (var a = 0, b = this.children; a < b.length;) {
                        var c = b[a];
                        ++a;
                        if (c.nodeType == W.Element) return c
                    }
                    return null
                },
                addChild: function(a) {
                    if (this.nodeType != W.Document && this.nodeType != W.Element) throw X.thrown("Bad node type, expected Element or Document but found " +
                        (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    null != a.parent && a.parent.removeChild(a);
                    this.children.push(a);
                    a.parent = this
                },
                removeChild: function(a) {
                    if (this.nodeType != W.Document && this.nodeType != W.Element) throw X.thrown("Bad node type, expected Element or Document but found " + (null == this.nodeType ? "null" : Cb.toString(this.nodeType)));
                    return N.remove(this.children, a) ? (a.parent = null, !0) : !1
                },
                toString: function() {
                    return Df.print(this)
                },
                __class__: W
            };
            var Pb = function() {
                this.keyShift = this.keyCtrl = this.isSolid = !1;
                this.rWidth = this.rHeight = 0;
                this.update = new ec;
                this.keyEvent = new ah;
                ka.call(this)
            };
            g["com.watabou.coogee.Scene"] = Pb;
            Pb.__name__ = "com.watabou.coogee.Scene";
            Pb.__super__ = ka;
            Pb.prototype = v(ka.prototype, {
                activate: function() {
                    rb.get_tick().add(l(this, this.onUpdate));
                    this.stage.addEventListener("keyDown", l(this, this.onKeyDown));
                    this.stage.addEventListener("keyUp", l(this, this.onKeyUp))
                },
                deactivate: function() {
                    rb.get_tick().remove(l(this, this.onUpdate));
                    this.stage.removeEventListener("keyDown", l(this, this.onKeyDown));
                    this.stage.removeEventListener("keyUp", l(this, this.onKeyUp))
                },
                onEsc: function() {
                    bb.quit()
                },
                onKeyDown: function(a) {
                    switch (a.keyCode) {
                        case 15:
                        case 17:
                            this.keyCtrl = !0;
                            break;
                        case 16:
                            this.keyShift = !0;
                            break;
                        case 27:
                            this.onEsc()
                    }
                    this.keyEvent.dispatch(a.keyCode, !0);
                    null == this.stage || this.stage.get_focus() instanceof sc || a.preventDefault()
                },
                onKeyUp: function(a) {
                    switch (a.keyCode) {
                        case 15:
                        case 17:
                            this.keyCtrl = !1;
                            break;
                        case 16:
                            this.keyShift = !1
                    }
                    this.keyEvent.dispatch(a.keyCode, !1);
                    this.stage.get_focus() instanceof
                    sc || a.preventDefault()
                },
                setSize: function(a, b) {
                    this.rWidth = a;
                    this.rHeight = b;
                    this.layout()
                },
                get_width: function() {
                    return this.rWidth
                },
                get_height: function() {
                    return this.rHeight
                },
                layout: function() {},
                onUpdate: function(a) {
                    this.update.dispatch(a)
                },
                __hitTest: function(a, b, c, d, f, h) {
                    a = ka.prototype.__hitTest.call(this, a, b, c, d, f, h);
                    return this.isSolid ? (a || d.push(h), !0) : a
                },
                __class__: Pb
            });
            var U = function() {
                this.valign = "top";
                this.halign = "left";
                this.rWidth = this.rHeight = 0;
                ka.call(this)
            };
            g["com.watabou.coogee.ui.View"] =
                U;
            U.__name__ = "com.watabou.coogee.ui.View";
            U.__super__ = ka;
            U.prototype = v(ka.prototype, {
                get_width: function() {
                    return this.rWidth
                },
                set_width: function(a) {
                    this.setSize(a, this.rHeight);
                    return this.rWidth
                },
                get_height: function() {
                    return this.rHeight
                },
                set_height: function(a) {
                    this.setSize(this.rWidth, a);
                    return this.rHeight
                },
                setSize: function(a, b) {
                    this.rWidth = a;
                    this.rHeight = b;
                    this.layout()
                },
                layout: function() {},
                add: function(a) {
                    this.addChild(a)
                },
                wipe: function() {
                    this.removeChildren()
                },
                set_enabled: function(a) {
                    return this.mouseEnabled =
                        this.mouseChildren = a
                },
                __class__: U,
                __properties__: v(ka.prototype.__properties__, {
                    set_enabled: "set_enabled"
                })
            });
            var Hb = function() {
                this.click = new Nc;
                U.call(this);
                this.label = this.createLabel();
                this.addChild(this.label);
                this.set_buttonMode(!0);
                this.addEventListener("click", l(this, this.onClick));
                this.setSize(this.label.get_width() + 10, this.label.get_height() + 10)
            };
            g["com.watabou.coogee.ui.ButtonBase"] = Hb;
            Hb.__name__ = "com.watabou.coogee.ui.ButtonBase";
            Hb.__super__ = U;
            Hb.prototype = v(U.prototype, {
                layout: function() {
                    this.get_graphics().clear();
                    this.get_graphics().beginFill(D.black);
                    this.get_graphics().drawRoundRect(0, 0, this.rWidth, this.rHeight, 10);
                    this.label.set_x((this.rWidth - this.label.get_width()) / 2 | 0);
                    this.label.set_y((this.rHeight - this.label.get_height()) / 2 | 0)
                },
                createLabel: function() {
                    return null
                },
                onClick: function(a) {
                    this.click.dispatch()
                },
                __class__: Hb
            });
            var fb = function(a, b) {
                this.txtLabel = a;
                Hb.call(this);
                null != b && this.click.add(b)
            };
            g["com.watabou.coogee.ui.Button"] = fb;
            fb.__name__ = "com.watabou.coogee.ui.Button";
            fb.__super__ = Hb;
            fb.prototype =
                v(Hb.prototype, {
                    createLabel: function() {
                        return ld.get(this.txtLabel, D.format(D.uiFont, D.smallSize, D.white))
                    },
                    __class__: fb
                });
            var gb = function() {
                this.snap = !0;
                this.margin = this.gap = 10;
                U.call(this)
            };
            g["com.watabou.coogee.ui.layouts.VBox"] = gb;
            gb.__name__ = "com.watabou.coogee.ui.layouts.VBox";
            gb.__super__ = U;
            gb.prototype = v(U.prototype, {
                layout: function() {
                    for (var a = this.margin, b = 0, c = !1, d = 0, f = this.get_numChildren(); d < f;) {
                        var h = d++;
                        h = this.getChildAt(h);
                        "left" != h.halign && (c = !0);
                        b < h.get_width() && (b = h.get_width());
                        h.set_x(this.margin);
                        h.set_y(a);
                        a += h.get_height() + this.gap;
                        this.snap && (a |= 0)
                    }
                    if (c)
                        for (d = 0, f = this.get_numChildren(); d < f;) switch (h = d++, h = this.getChildAt(h), h.halign) {
                            case "center":
                                h.set_x(this.margin + (b - h.get_width()) / 2);
                                break;
                            case "fill":
                                h.set_width(b);
                                break;
                            case "right":
                                h.set_x(this.margin + (b - h.get_width()))
                        }
                    this.rHeight = a > this.margin ? a - this.gap + this.margin : 2 * this.margin;
                    this.rWidth = b + 2 * this.margin
                },
                add: function(a) {
                    U.prototype.add.call(this, a);
                    this.layout()
                },
                setMargins: function(a, b) {
                    this.margin = a;
                    this.gap = b
                },
                __class__: gb
            });
            var oc = function() {
                gb.call(this);
                this.setMargins(0, 0);
                this.addEventListener("keyDown", l(this, this.onKeyDown));
                this.addEventListener("focusOut", l(this, this.onFocusOut))
            };
            g["com.watabou.coogee.ui.Form"] = oc;
            oc.__name__ = "com.watabou.coogee.ui.Form";
            oc.__super__ = gb;
            oc.prototype = v(gb.prototype, {
                getTitle: function() {
                    return null
                },
                onShow: function() {},
                onHide: function() {},
                onKeyDown: function(a) {
                    this.onKey(a.keyCode) && (a.stopPropagation(), a.preventDefault())
                },
                onKey: function(a) {
                    switch (a) {
                        case 13:
                            this.onEnter();
                            break;
                        case 27:
                            this.onEsc();
                            break;
                        default:
                            return !1
                    }
                    return !0
                },
                onEsc: function() {
                    this.dialog.hide()
                },
                onEnter: function() {},
                onFocusOut: function(a) {
                    null == a.relatedObject && this.stage.set_focus(this)
                },
                __class__: oc
            });
            var ic = function(a) {
                oc.call(this);
                this.buttons = new Ef(a);
                this.buttons.click.add(l(this, this.onButton));
                this.addChild(this.buttons)
            };
            g["com.watabou.coogee.ui.ButtonsForm"] = ic;
            ic.__name__ = "com.watabou.coogee.ui.ButtonsForm";
            ic.__super__ = oc;
            ic.prototype = v(oc.prototype, {
                add: function(a) {
                    this.addChild(a);
                    this.addChild(this.buttons);
                    this.layout()
                },
                layout: function() {
                    oc.prototype.layout.call(this);
                    this.buttons.set_width(this.rWidth);
                    oc.prototype.layout.call(this)
                },
                onButton: function(a) {
                    this.dialog.hide()
                },
                onEnter: function() {
                    this.onButton("OK")
                },
                onEsc: function() {
                    this.onButton("Cancel")
                },
                __class__: ic
            });
            var ud = function(a) {
                this.changed = new ec;
                U.call(this);
                this.border = ta.black();
                this.add(this.border);
                this.empty = ta.white();
                this.add(this.empty);
                this.filled = ta.black();
                this.add(this.filled);
                null != a ? (this.label =
                    new Ib(a), this.label.mouseEnabled = !0, this.label.mouseChildren = !0, this.add(this.label), this.setSize(24 + this.label.get_width(), Math.max(20, this.label.get_height()))) : this.setSize(20, 20);
                this.set_buttonMode(!0);
                this.addEventListener("click", l(this, this.onClick))
            };
            g["com.watabou.coogee.ui.CheckBox"] = ud;
            ud.__name__ = "com.watabou.coogee.ui.CheckBox";
            ud.__super__ = U;
            ud.prototype = v(U.prototype, {
                layout: function() {
                    this.border.setSize(20, 20);
                    this.border.set_x(null != this.label ? 0 : (this.rWidth - this.border.get_width()) /
                        2 | 0);
                    this.border.set_y((this.rHeight - this.border.get_height()) / 2 | 0);
                    this.empty.setSize(this.border.get_width() - 4, this.border.get_height() - 4);
                    this.empty.set_x(this.border.get_x() + 2);
                    this.empty.set_y(this.border.get_y() + 2);
                    this.filled.setSize(this.empty.get_width() - 4, this.empty.get_height() - 4);
                    this.filled.set_x(this.empty.get_x() + 2);
                    this.filled.set_y(this.empty.get_y() + 2);
                    null != this.label && this.label.set_x(this.border.get_width() + 4)
                },
                get_value: function() {
                    return this.filled.get_visible()
                },
                set_value: function(a) {
                    this.filled.set_visible(a);
                    return a
                },
                onClick: function(a) {
                    this.set_value(!this.get_value());
                    this.changed.dispatch(this.get_value())
                },
                get_text: function() {
                    return null != this.label ? this.label.get_text() : null
                },
                set_enabled: function(a) {
                    U.prototype.set_enabled.call(this, a);
                    var b = a ? D.black : D.light;
                    this.border.bmp.get_bitmapData().setPixel(0, 0, b);
                    this.filled.bmp.get_bitmapData().setPixel(0, 0, b);
                    null != this.label && this.label.set_color(b);
                    return a
                },
                __class__: ud,
                __properties__: v(U.prototype.__properties__, {
                    get_text: "get_text",
                    set_value: "set_value",
                    get_value: "get_value"
                })
            });
            var hg = function(a, b) {
                null == b && (b = -1);
                var c = this;
                this.group = a;
                for (var d = 0, f = a.length; d < f;) {
                    var h = d++;
                    a[h].set_value(h == b)
                }
                for (d = 0; d < a.length;) b = [a[d]], ++d, b[0].changed.add(function(a) {
                    return function(b) {
                        c.changed(a[0], b)
                    }
                }(b))
            };
            g["com.watabou.coogee.ui.RadioGroup"] = hg;
            hg.__name__ = "com.watabou.coogee.ui.RadioGroup";
            hg.prototype = {
                changed: function(a, b) {
                    if (null == this.origin) {
                        this.origin = a;
                        if (b) {
                            b = 0;
                            for (var c = this.group; b < c.length;) {
                                var d = c[b];
                                ++b;
                                d != a && d.set_value(!1)
                            }
                        } else a.set_value(!0);
                        this.origin = null
                    }
                },
                __class__: hg
            };
            var Hd = function(a, b) {
                this.onMinimize = new ec;
                this.onMove = new ec;
                this.onHide = new ec;
                this.minimized = this.minimizable = !1;
                U.call(this);
                this.bg1 = ta.black();
                this.addChild(this.bg1);
                this.bg2 = ta.white();
                this.addChild(this.bg2);
                this.addEventListener("mouseDown", l(this, this.onBringUp));
                this.header = new Ff(b);
                this.header.addEventListener("mouseDown", l(this, this.onStartDrag));
                this.header.addEventListener("click", l(this, this.onClick));
                this.addChild(this.header);
                this.content = a;
                this.addChild(a);
                this.resize()
            };
            g["com.watabou.coogee.ui.Window"] = Hd;
            Hd.__name__ = "com.watabou.coogee.ui.Window";
            Hd.show = function(a, b, c, d) {
                c = null == d ? new Hd(b, c) : w.createInstance(d, [b, c]);
                c.set_x((a.get_width() - c.get_width()) / 2 | 0);
                c.set_y((a.get_height() - c.get_height()) / 2 | 0);
                a.addChild(c);
                null != b.stage && b.stage.set_focus(b);
                return c
            };
            Hd.__super__ = U;
            Hd.prototype = v(U.prototype, {
                resize: function(a) {
                    null == a && (a = !1);
                    var b = this.content.get_width() + 4,
                        c = this.header.get_height() + this.content.get_height() + 2,
                        d = a ? (this.rWidth - b) /
                        2 | 0 : 0;
                    a = a ? (this.rHeight - c) / 2 | 0 : 0;
                    this.setSize(b, c);
                    this.set_x(this.get_x() + d);
                    this.set_y(this.get_y() + a)
                },
                layout: function() {
                    this.header.set_width(this.rWidth);
                    this.minimized ? (this.bg1.setSize(this.rWidth, this.header.get_height()), this.content.set_visible(!1), this.bg2.set_visible(!1)) : (this.bg1.setSize(this.rWidth, this.rHeight), this.content.set_visible(!0), this.bg2.set_visible(!0), this.bg2.setSize(this.rWidth - 4, this.rHeight - this.header.get_height() - 2), this.content.set_x(this.bg2.set_x(2)), this.content.set_y(this.bg2.set_y(this.header.get_height())))
                },
                hide: function() {
                    null != this.parent && (this.stage.set_focus(this.stage), this.parent.removeChild(this), this.onHide.dispatch(this))
                },
                onBringUp: function(a) {
                    this.parent.addChild(this)
                },
                onStartDrag: function(a) {
                    this.clicked = !0;
                    this.stage.addEventListener("mouseMove", l(this, this.onDrag));
                    this.stage.addEventListener("mouseUp", l(this, this.onEndDrag));
                    this.grabX = this.get_mouseX();
                    this.grabY = this.get_mouseY()
                },
                onDrag: function(a) {
                    this.clicked = !1;
                    this.set_x(this.parent.get_mouseX() - this.grabX);
                    this.set_y(this.parent.get_mouseY() -
                        this.grabY);
                    a.updateAfterEvent()
                },
                onEndDrag: function(a) {
                    this.stage.removeEventListener("mouseMove", l(this, this.onDrag));
                    this.stage.removeEventListener("mouseUp", l(this, this.onEndDrag));
                    this.clicked || this.onMove.dispatch(this)
                },
                onClick: function(a) {
                    this.clicked && this.minimizable && (this.minimized = !this.minimized, this.layout(), this.onMinimize.dispatch(this))
                },
                setMinimized: function(a) {
                    this.minimized != a && (this.minimized = a);
                    this.layout()
                },
                setTitle: function(a) {
                    this.header.setText(a)
                },
                getAdjustment: function() {
                    if (null ==
                        this.parent) return null;
                    var a = this.header.getRect(this.parent),
                        b = a.height,
                        c = a.get_right() <= b ? 0 : a.get_left() >= this.parent.get_width() - b ? this.parent.get_width() - a.width : this.get_x();
                    a = 0 >= a.get_top() ? 0 : a.get_top() >= this.parent.get_height() - b ? this.parent.get_height() - a.height : this.get_y();
                    return c != this.get_x() || a != this.get_y() ? new I(c, a) : null
                },
                __class__: Hd
            });
            var ee = function(a, b) {
                a.dialog = this;
                Hd.call(this, a, b);
                this.header.close.add(function() {
                    a.onEsc()
                })
            };
            g["com.watabou.coogee.ui.Dialog"] = ee;
            ee.__name__ =
                "com.watabou.coogee.ui.Dialog";
            ee.show = function(a, b, c) {
                null == c && (c = b.getTitle());
                a = Hd.show(a, b, c, ee);
                b.onShow();
                return a
            };
            ee.__super__ = Hd;
            ee.prototype = v(Hd.prototype, {
                hide: function() {
                    if (null != this.parent) va.__cast(this.content, oc).onHide();
                    Hd.prototype.hide.call(this)
                },
                __class__: ee
            });
            var Rc = function(a, b) {
                this.update = new ec;
                var c = this;
                this.values = b;
                this.labels = a;
                U.call(this);
                this.border = ta.black();
                this.add(this.border);
                this.bg = ta.white();
                this.bg.set_x(this.bg.set_y(2));
                this.add(this.bg);
                this.tf = ld.get("",
                    D.format(D.uiFont, D.normalSize, D.black));
                this.addChild(this.tf);
                this.btn = new Va;
                this.btn.set_width(D.normalSize);
                this.btn.set_enabled(!1);
                this.add(this.btn);
                for (var d = 0, f = 0, h = 0; h < a.length;) {
                    var k = a[h];
                    ++h;
                    this.tf.set_text(k);
                    d = Math.max(d, this.tf.get_width());
                    f = Math.max(f, this.tf.get_height())
                }
                this.tf.set_autoSize(2);
                this.menu = new dd;
                h = [];
                k = 0;
                for (var n = a.length; k < n;) {
                    var p = [k++];
                    h.push(this.menu.addItem(a[p[0]], function(d) {
                        return function() {
                            c.onSelect(a[d[0]], b[d[0]])
                        }
                    }(p)))
                }
                this.items = h;
                this.set_buttonMode(!0);
                this.addEventListener("click", l(this, this.onClick));
                this.addEventListener("mouseWheel", l(this, this.onWheel));
                0 < a.length && this.set_text(a[0]);
                this.setSize(Math.ceil(d + this.btn.get_width()), Math.ceil(f))
            };
            g["com.watabou.coogee.ui.DropDown"] = Rc;
            Rc.__name__ = "com.watabou.coogee.ui.DropDown";
            Rc.ofStrings = function(a) {
                return new Rc(a, a)
            };
            Rc.ofInts = function(a) {
                for (var b = [], c = 0, d = a.length; c < d;) {
                    var f = c++;
                    b.push(f)
                }
                return new Rc(a, b)
            };
            Rc.__super__ = U;
            Rc.prototype = v(U.prototype, {
                layout: function() {
                    this.border.setSize(this.rWidth,
                        this.rHeight);
                    this.bg.setSize(this.rWidth - 4, this.rHeight - 4);
                    this.tf.set_width(this.rWidth - this.btn.get_width());
                    this.tf.set_height(this.rHeight);
                    this.btn.set_height(this.rHeight);
                    this.btn.set_x(this.rWidth - this.btn.get_width());
                    this.btn.set_y((this.rHeight - this.btn.get_height()) / 2)
                },
                set_text: function(a) {
                    this.tf.set_text(a);
                    return a
                },
                get_value: function() {
                    var a = this.labels.indexOf(this.tf.get_text());
                    return -1 == a ? null : this.values[a]
                },
                set_value: function(a) {
                    for (var b = 0, c = this.values.length; b < c;) {
                        var d =
                            b++;
                        if (this.values[d] == a) return this.tf.set_text(this.labels[d]), a
                    }
                    return this.get_value()
                },
                set_centered: function(a) {
                    var b = this.tf.get_defaultTextFormat();
                    b.align = a ? 0 : 3;
                    this.tf.setTextFormat(b);
                    this.tf.set_defaultTextFormat(b);
                    return a
                },
                onClick: function(a) {
                    a = this.labels.indexOf(this.tf.get_text());
                    for (var b = 0, c = this.items.length; b < c;) {
                        var d = b++;
                        this.items[d].setCheck(d == a)
                    }
                    a = u.getRect(this);
                    u.showMenuAt(this.menu, Math.ceil(a.get_left()), a.get_bottom() - 2)
                },
                onWheel: function(a) {
                    u.hideMenu();
                    var b = this.values.indexOf(this.get_value());
                    if (-1 != b && (a = a.delta, b += 0 == a ? 0 : 0 > a ? -1 : 1, 0 <= b && b < this.values.length)) this.onSelect(this.labels[b], this.values[b])
                },
                onSelect: function(a, b) {
                    this.stage.set_focus(this);
                    this.tf.set_text(a);
                    this.update.dispatch(b)
                },
                __class__: Rc,
                __properties__: v(U.prototype.__properties__, {
                    set_centered: "set_centered",
                    set_value: "set_value",
                    get_value: "get_value",
                    set_text: "set_text"
                })
            });
            var Ib = function(a, b) {
                null == b && (b = !1);
                U.call(this);
                this.tf = ld.get(a, D.format(D.uiFont, b ? D.smallSize : D.normalSize, D.black));
                this.tf.set_x(-2);
                this.tf.set_y(-2);
                this.addChild(this.tf);
                this.setSize(this.tf.get_width() - 4, this.tf.get_height() - 4)
            };
            g["com.watabou.coogee.ui.Label"] = Ib;
            Ib.__name__ = "com.watabou.coogee.ui.Label";
            Ib.__super__ = U;
            Ib.prototype = v(U.prototype, {
                get_text: function() {
                    return this.tf.get_text()
                },
                set_text: function(a) {
                    this.tf.set_text(a);
                    this.setSize(this.tf.get_width(), this.tf.get_height());
                    return a
                },
                set_color: function(a) {
                    var b = this.tf.get_defaultTextFormat();
                    b.color = a;
                    this.tf.setTextFormat(b);
                    this.tf.set_defaultTextFormat(b);
                    return a
                },
                __class__: Ib,
                __properties__: v(U.prototype.__properties__, {
                    set_color: "set_color",
                    set_text: "set_text",
                    get_text: "get_text"
                })
            });
            var dd = function() {
                U.call(this);
                this.bg = ta.black();
                this.addChild(this.bg);
                this.items = [];
                this.addEventListener("addedToStage", l(this, this.onAdded));
                this.addEventListener("removedFromStage", l(this, this.onRemoved))
            };
            g["com.watabou.coogee.ui.Menu"] = dd;
            dd.__name__ = "com.watabou.coogee.ui.Menu";
            dd.__super__ = U;
            dd.prototype = v(U.prototype, {
                layout: function() {
                    for (var a = this.rHeight =
                            this.rWidth = 0, b = this.items; a < b.length;) {
                        var c = b[a];
                        ++a;
                        this.rWidth = Math.max(this.rWidth, c.get_width());
                        this.rHeight += c.get_height()
                    }
                    var d = 2;
                    a = 0;
                    for (b = this.items; a < b.length;) c = b[a], ++a, c.set_width(this.rWidth), c.set_x(2), c.set_y(d), d += c.get_height();
                    this.rWidth += 4;
                    this.rHeight += 4;
                    this.bg.setSize(this.rWidth, this.rHeight)
                },
                add: function(a) {
                    this.items.push(a);
                    this.addChild(a);
                    this.layout()
                },
                remove: function(a) {
                    N.remove(this.items, a);
                    this.removeChild(a);
                    this.layout()
                },
                addItem: function(a, b, c) {
                    null == c && (c = !1);
                    a = new ff(a, null, b);
                    a.setCheck(c);
                    this.add(a);
                    return a
                },
                addSubmenu: function(a, b) {
                    a = new ff(a, b);
                    this.add(a);
                    return a
                },
                addSeparator: function() {
                    0 < this.items.length && !(this.items[this.items.length - 1] instanceof Je) && this.add(new Je)
                },
                hide: function() {
                    null != this.submenu && (this.submenu.hide(), this.submenu = null);
                    null != this.parent && (null != this.stage && this.stage.set_focus(this.stage), this.parent.removeChild(this))
                },
                getRoot: function() {
                    return null != this.parentMenu ? this.parentMenu.getRoot() : this
                },
                onMouseDown: function(a) {
                    this.hitTestPoint(this.stage.get_mouseX(),
                        this.stage.get_mouseY()) || this.hide()
                },
                onAdded: function(a) {
                    this.stage.addEventListener("mouseDown", l(this, this.onMouseDown));
                    this.stage.addEventListener("rightMouseDown", l(this, this.onMouseDown))
                },
                onRemoved: function(a) {
                    this.stage.removeEventListener("mouseDown", l(this, this.onMouseDown));
                    this.stage.removeEventListener("rightMouseDown", l(this, this.onMouseDown))
                },
                cancel: function() {
                    null != this.submenu && (this.submenu.cancel(), this.parent.removeChild(this.submenu), this.submenu = null)
                },
                hover: function(a, b) {
                    this.cancel();
                    if (null != b) {
                        b.parentMenu = this;
                        this.submenu = b;
                        var c = a.get_x() + a.rWidth,
                            d = a.get_y() - 2,
                            f = new I(c, d);
                        f = this.parent.globalToLocal(this.localToGlobal(f));
                        if (this.parent == u.layer) {
                            var h = !1;
                            f.x + b.get_width() > this.parent.get_width() && (c = a.get_x() - b.get_width(), h = !0);
                            f.y + b.get_height() > this.parent.get_height() && (d = a.get_y() + a.rHeight - b.get_height() + 2, h = !0);
                            h && (f.x = c, f.y = d, f = this.parent.globalToLocal(this.localToGlobal(f)))
                        }
                        b.set_x(f.x);
                        b.set_y(f.y);
                        this.parent.addChild(b)
                    }
                },
                __class__: dd
            });
            var ff = function(a,
                b, c) {
                var d = this;
                U.call(this);
                this.submenu = b;
                this.callback = c;
                this.bg = ta.white();
                this.addChild(this.bg);
                this.bullet = ta.black();
                this.bullet.set_visible(!1);
                this.bullet.setSize(8, 8);
                this.addChild(this.bullet);
                c = null == c && null == b;
                this.formatNormal = D.format(D.uiFont, D.smallSize, c ? D.medium : D.black);
                this.formatHover = D.format(D.uiFont, D.smallSize, D.white);
                this.tf = ld.get(a, this.formatNormal);
                this.addChild(this.tf);
                null != b && (this.sub = ld.get(" >", this.formatNormal), this.addChild(this.sub));
                this.addEventListener("mouseDown",
                    function(a) {
                        a.stopPropagation()
                    });
                c || (this.set_buttonMode(!0), this.addEventListener("rollOver", function(a) {
                    d.hover(!0)
                }), this.addEventListener("rollOut", function(a) {
                    d.hover(!1)
                }), this.addEventListener("click", l(this, this.onClick)));
                a = Math.round(D.normalSize / 4);
                this.bullet.set_x(a);
                this.tf.set_x(a + this.bullet.get_width() + a - 2);
                null != b ? (this.sub.set_x(this.tf.get_x() + this.tf.get_width() - 4), this.rWidth = this.sub.get_x() + (this.sub.get_width() - 4) + a) : this.rWidth = a + this.bullet.get_width() + a + (this.tf.get_width() -
                    4) + a;
                this.rHeight = 1.5 * D.normalSize;
                this.rWidth = Math.ceil(this.rWidth);
                this.rHeight = Math.ceil(this.rHeight);
                this.layout()
            };
            g["com.watabou.coogee.ui.MenuItem"] = ff;
            ff.__name__ = "com.watabou.coogee.ui.MenuItem";
            ff.__super__ = U;
            ff.prototype = v(U.prototype, {
                layout: function() {
                    this.bg.setSize(this.rWidth, this.rHeight);
                    this.bullet.set_y((this.rHeight - this.bullet.get_height()) / 2);
                    this.tf.set_y((this.rHeight - this.tf.get_height()) / 2);
                    null != this.sub && (this.sub.set_y(this.tf.get_y()), this.sub.set_x(this.rWidth - this.bullet.get_x() -
                        this.sub.get_width() + 2))
                },
                hover: function(a) {
                    null != this.get_root() && (this.highlight(a), va.__cast(this.parent, dd).hover(this, this.submenu))
                },
                highlight: function(a) {
                    var b = a ? this.formatHover : this.formatNormal;
                    this.tf.set_defaultTextFormat(b);
                    var c = a ? D.black : D.white;
                    this.bg.bmp.get_bitmapData().setPixel(0, 0, c);
                    c = a ? D.white : D.black;
                    this.bullet.bmp.get_bitmapData().setPixel(0, 0, c);
                    null != this.submenu && this.sub.set_defaultTextFormat(b)
                },
                onClick: function(a) {
                    null == this.submenu ? (a.stopPropagation(), this.highlight(!1),
                        va.__cast(this.parent, dd).getRoot().hide(), null != this.callback && this.callback()) : va.__cast(this.parent, dd).hover(this, null == this.submenu.parent ? this.submenu : null)
                },
                setCheck: function(a) {
                    this.bullet.set_visible(a);
                    return this
                },
                __class__: ff
            });
            var ta = function(a, b) {
                null == b && (b = 1);
                U.call(this);
                this.set_alpha(b);
                this.bmp = new Nd(new Fb(1, 1, !1, a));
                this.addChild(this.bmp)
            };
            g["com.watabou.coogee.ui.SolidRect"] = ta;
            ta.__name__ = "com.watabou.coogee.ui.SolidRect";
            ta.black = function() {
                return new ta(D.black)
            };
            ta.light =
                function() {
                    return new ta(D.light)
                };
            ta.white = function() {
                return new ta(D.white)
            };
            ta.__super__ = U;
            ta.prototype = v(U.prototype, {
                layout: function() {
                    this.bmp.set_width(this.rWidth);
                    this.bmp.set_height(this.rHeight)
                },
                __class__: ta
            });
            var Je = function() {
                ta.call(this, D.black);
                this.setSize(2, 2)
            };
            g["com.watabou.coogee.ui.MenuSeparator"] = Je;
            Je.__name__ = "com.watabou.coogee.ui.MenuSeparator";
            Je.__super__ = ta;
            Je.prototype = v(ta.prototype, {
                __class__: Je
            });
            var fe = function(a, b, c) {
                this.action = new ec;
                var d = this;
                this.txtLabel =
                    a;
                Hb.call(this);
                this.menu = new dd;
                a = 0;
                for (var f = b.length; a < f;) {
                    var h = a++,
                        k = b[h];
                    this.menu.addItem(k, function(a) {
                        return function() {
                            d.action.dispatch(a[0])
                        }
                    }([null != c ? c[h] : k]))
                }
            };
            g["com.watabou.coogee.ui.MultiAction"] = fe;
            fe.__name__ = "com.watabou.coogee.ui.MultiAction";
            fe.__super__ = Hb;
            fe.prototype = v(Hb.prototype, {
                createLabel: function() {
                    var a = this,
                        b = new ed;
                    b.setMargins(2, 4);
                    var c = new Ib(this.txtLabel, !0);
                    c.set_color(D.white);
                    b.add(c);
                    c = new Va(D.white);
                    c.click.add(function() {
                        a.onClick(null)
                    });
                    c.valign =
                        "fill";
                    b.add(c);
                    return b
                },
                onClick: function(a) {
                    Hb.prototype.onClick.call(this, a);
                    a = u.getRect(this);
                    u.showMenuAt(this.menu, a.get_left(), a.get_bottom())
                },
                __class__: fe
            });
            var $d = function(a, b, c) {
                null == c && (c = 0);
                null == b && (b = 100);
                null == a && (a = 0);
                this.submit = new ec;
                this.change = new ec;
                this.cycled = !1;
                U.call(this);
                this.min = a;
                this.max = b;
                this.rounding = Math.pow(10, c);
                this.bg = new ka;
                this.bg.addEventListener("mouseDown", l(this, this.onPage));
                this.addChild(this.bg);
                this.scale = ta.black();
                this.scale.mouseEnabled = !1;
                this.add(this.scale);
                this.thumb = ta.black();
                this.thumb.set_buttonMode(!0);
                this.thumb.addEventListener("mouseDown", l(this, this.onStartDrag));
                this.add(this.thumb);
                this.addEventListener("mouseWheel", l(this, this.onWheel));
                this._value = a;
                this._prev = NaN;
                this.setSize(200, 20)
            };
            g["com.watabou.coogee.ui.Slider"] = $d;
            $d.__name__ = "com.watabou.coogee.ui.Slider";
            $d.__super__ = U;
            $d.prototype = v(U.prototype, {
                get_value: function() {
                    return this._value
                },
                set_value: function(a) {
                    this._value = this.cycled ? Fc.cycle(a, this.min, this.max) : Fc.gate(a, this.min,
                        this.max);
                    this._value = Math.round(this._value * this.rounding) / this.rounding;
                    this._prev != this._value && (this._prev = this._value, this.placeThumb(), this.change.dispatch(this._value));
                    return a
                },
                layout: function() {
                    var a = this.bg.get_graphics();
                    a.clear();
                    a.beginFill(16711680, 0);
                    a.drawRect(0, 0, this.rWidth, this.rHeight);
                    this.scale.setSize(this.rWidth, 2);
                    this.scale.set_y((this.rHeight - this.scale.get_height()) / 2 | 0);
                    this.thumb.setSize(10, this.rHeight);
                    this.thumb.set_y((this.rHeight - this.thumb.get_height()) / 2 | 0);
                    this.placeThumb()
                },
                placeThumb: function() {
                    this.thumb.set_x((this.rWidth - this.thumb.get_width()) * (this.get_value() - this.min) / (this.max - this.min))
                },
                onStartDrag: function(a) {
                    this.stage.addEventListener("mouseMove", l(this, this.onDrag));
                    this.stage.addEventListener("mouseUp", l(this, this.onEndDrag));
                    this.grabX = this.thumb.get_mouseX()
                },
                onDrag: function(a) {
                    this.set_value((this.get_mouseX() - this.grabX) / (this.rWidth - this.thumb.get_width()) * (this.max - this.min) + this.min);
                    a.updateAfterEvent()
                },
                onEndDrag: function(a) {
                    this.stage.removeEventListener("mouseMove",
                        l(this, this.onDrag));
                    this.stage.removeEventListener("mouseUp", l(this, this.onEndDrag));
                    this.submit.dispatch(this.get_value())
                },
                onPage: function(a) {
                    a = this.get_value();
                    this.get_mouseX() < this.thumb.get_x() ? this.set_value(this.get_value() - .1 * (this.max - this.min)) : this.get_mouseX() > this.thumb.get_x() + this.thumb.get_width() && this.set_value(this.get_value() + .1 * (this.max - this.min));
                    a != this.get_value() && this.submit.dispatch(this.get_value())
                },
                onWheel: function(a) {
                    var b = this.get_value(),
                        c = this.get_value();
                    a = a.delta;
                    this.set_value(c + (0 == a ? 0 : 0 > a ? -1 : 1) * this.rounding);
                    b != this.get_value() && this.submit.dispatch(this.get_value())
                },
                __class__: $d,
                __properties__: v(U.prototype.__properties__, {
                    set_value: "set_value",
                    get_value: "get_value"
                })
            });
            var Jb = function(a) {
                this.enter = new ec;
                this.update = new ec;
                U.call(this);
                this.tf = ld.get(a, D.format(D.uiFont, D.normalSize, D.black), l(this, this.onUpdate));
                this.tf.set_border(!0);
                this.tf.set_borderColor(D.black);
                this.tf.set_autoSize(2);
                this.tf.set_wordWrap(!0);
                this.addChild(this.tf);
                this.addEventListener("focusIn",
                    l(this, this.onFocusIn));
                this.tf.addEventListener("keyDown", l(this, this.onKeyDown));
                this.setSize(this.tf.get_width(), this.tf.get_height())
            };
            g["com.watabou.coogee.ui.TextArea"] = Jb;
            Jb.__name__ = "com.watabou.coogee.ui.TextArea";
            Jb.__super__ = U;
            Jb.prototype = v(U.prototype, {
                layout: function() {
                    this.tf.set_width(this.rWidth);
                    this.tf.set_height(this.rHeight)
                },
                onFocusIn: function(a) {
                    this.stage.set_focus(this.tf);
                    this.tf.setSelection(this.tf.get_text().length, this.tf.get_text().length)
                },
                onKeyDown: function(a) {
                    switch (a.keyCode) {
                        case 13:
                            this.enter.dispatch(this.tf.get_text());
                            break;
                        case 27:
                            this.stage.set_focus(this.parent)
                    }
                    a.stopPropagation()
                },
                onUpdate: function() {
                    this.update.dispatch(this.tf.get_text())
                },
                __class__: Jb
            });
            var tc = function(a, b) {
                null == b && (b = !1);
                null == a && (a = "");
                this.leave = new Nc;
                this.enter = new ec;
                this.update = new ec;
                U.call(this);
                this.tf = ld.input(a, D.format(b ? D.uiFontMono : D.uiFont, D.normalSize, D.black), l(this, this.onUpdate));
                this.tf.set_backgroundColor(D.white);
                this.tf.set_borderColor(D.black);
                this.addChild(this.tf);
                this.addEventListener("focusIn", l(this, this.onFocusIn));
                this.addEventListener("focusOut", l(this, this.onFocusOut));
                this.addEventListener("keyDown", l(this, this.onKeyDown));
                this.setSize(this.tf.get_width(), this.tf.get_height())
            };
            g["com.watabou.coogee.ui.TextInput"] = tc;
            tc.__name__ = "com.watabou.coogee.ui.TextInput";
            tc.__super__ = U;
            tc.prototype = v(U.prototype, {
                onKeyDown: function(a) {
                    switch (a.keyCode) {
                        case 13:
                            this.enter.dispatch(this.tf.get_text());
                            break;
                        case 27:
                            this.stage.set_focus(this.parent)
                    }
                    a.stopPropagation()
                },
                onFocusIn: function(a) {
                    this.stage.set_focus(this.tf)
                },
                onFocusOut: function(a) {
                    this.leave.dispatch()
                },
                layout: function() {
                    this.tf.set_width(this.rWidth);
                    this.tf.set_height(this.rHeight);
                    this.layoutPrompt()
                },
                layoutPrompt: function() {
                    if (null != this.label) {
                        var a = this.get_centered();
                        this.label.set_x(a ? (this.rWidth - this.label.get_width()) / 2 : 2);
                        this.label.set_y(a ? (this.rHeight - this.label.get_height()) / 2 : 2)
                    }
                },
                get_text: function() {
                    return this.tf.get_text()
                },
                set_text: function(a) {
                    this.tf.set_text(a);
                    this.updatePrompt();
                    return a
                },
                get_centered: function() {
                    return 0 == this.tf.get_defaultTextFormat().align
                },
                set_centered: function(a) {
                    var b = this.tf.get_defaultTextFormat();
                    b.align = a ? 0 : 3;
                    this.tf.setTextFormat(b);
                    this.tf.set_defaultTextFormat(b);
                    this.layoutPrompt();
                    return a
                },
                set_restrict: function(a) {
                    return this.tf.set_restrict(a)
                },
                onUpdate: function() {
                    this.updatePrompt();
                    this.update.dispatch(this.tf.get_text())
                },
                updatePrompt: function() {
                    null != this.label && (this.layoutPrompt(), this.label.set_visible("" == this.tf.get_text()))
                },
                set_prompt: function(a) {
                    null == this.label && (this.label = new Ib(a), this.label.set_enabled(!1),
                        this.label.set_color(D.medium), this.add(this.label), this.updatePrompt());
                    this.label.set_text(a);
                    return a
                },
                selecteAll: function() {
                    this.tf.setSelection(0, this.tf.get_length())
                },
                set_enabled: function(a) {
                    U.prototype.set_enabled.call(this, a);
                    this.tf.set_alpha(a ? 1 : .6);
                    return this.tf.mouseEnabled = a
                },
                __class__: tc,
                __properties__: v(U.prototype.__properties__, {
                    set_prompt: "set_prompt",
                    set_restrict: "set_restrict",
                    set_centered: "set_centered",
                    get_centered: "get_centered",
                    set_text: "set_text",
                    get_text: "get_text"
                })
            });
            var q = function(a) {
                var b = this;
                U.call(this);
                a = ld.get(a, D.format(D.uiFont, D.normalSize, D.white));
                a.set_x(10);
                a.set_y(10);
                this.addChild(a);
                this.rWidth = 10 + a.get_width() + 10;
                this.rHeight = 10 + a.get_height() + 10;
                this.get_graphics().beginFill(D.black);
                this.get_graphics().drawRoundRect(0, 0, this.rWidth, this.rHeight, 10);
                Ke.run(q.delay, function(a) {
                    b.set_alpha(q.delay * (1 - a))
                }).onComplete(function() {
                    null != b.parent && b.parent.removeChild(b)
                })
            };
            g["com.watabou.coogee.ui.Toast"] = q;
            q.__name__ = "com.watabou.coogee.ui.Toast";
            q.show = function(a) {
                q.hide();
                var b = u.layer;
                a = q.instance = new q(a);
                a.set_x((b.get_width() - a.get_width()) / 2 | 0);
                a.set_y(b.get_height() - a.get_height() - q.offset | 0);
                b.addChild(a);
                return a
            };
            q.hide = function() {
                null != q.instance && null != q.instance.parent && q.instance.parent.removeChild(q.instance)
            };
            q.__super__ = U;
            q.prototype = v(U.prototype, {
                __class__: q
            });
            var u = function() {};
            g["com.watabou.coogee.ui.UI"] = u;
            u.__name__ = "com.watabou.coogee.ui.UI";
            u.showMenu = function(a) {
                u.showMenuAt(a, u.layer.get_mouseX(), u.layer.get_mouseY())
            };
            u.showMenuAt = function(a, b, c) {
                var d = a.items;
                0 < d.length && d[d.length - 1] instanceof Je && a.remove(d[d.length - 1]);
                u.menu = a;
                a.set_x((b + a.get_width() <= u.layer.get_width() ? b : b - a.get_width()) | 0);
                a.set_y((c + a.get_height() <= u.layer.get_height() ? c : c - a.get_height()) | 0);
                0 > a.get_x() && a.set_x(0);
                0 > a.get_y() && a.set_y(0);
                u.layer.addChild(a)
            };
            u.hideMenu = function() {
                return null != u.menu && null != u.menu.stage ? (u.menu.hide(), u.menu = null, !0) : !1
            };
            u.showDialog = function(a, b) {
                a = ee.show(u.layer, a, b);
                a.onHide.add(function(a) {
                    N.remove(u.windows,
                        a)
                });
                u.windows.push(a);
                return a
            };
            u.wipe = function() {
                u.hideMenu();
                for (var a = 0, b = u.windows.slice(); a < b.length;) {
                    var c = b[a];
                    ++a;
                    c.hide()
                }
                u.windows = []
            };
            u.findWidnow = function(a) {
                for (var b = 0, c = u.windows; b < c.length;) {
                    var d = c[b];
                    ++b;
                    if (va.__instanceof(d.content, a)) return d
                }
                return null
            };
            u.findForm = function(a) {
                for (var b = 0, c = u.windows; b < c.length;) {
                    var d = c[b];
                    ++b;
                    if (va.__instanceof(d.content, a)) return d.content
                }
                return null
            };
            u.getRect = function(a) {
                var b = new I(a.get_x(), a.get_y()),
                    c = new I(a.get_x() + a.rWidth, a.get_y() +
                        a.rHeight);
                b = u.layer.globalToLocal(a.parent.localToGlobal(b));
                c = u.layer.globalToLocal(a.parent.localToGlobal(c));
                return new na(b.x, b.y, c.x - b.x, c.y - b.y)
            };
            var D = function() {};
            g["com.watabou.coogee.ui.UIStyle"] = D;
            D.__name__ = "com.watabou.coogee.ui.UIStyle";
            D.format = function(a, b, c) {
                null == c && (c = 0);
                return new we("_" == a.charAt(0) ? a : ac.getFont(a).name, b, c)
            };
            D.useDefault = function() {
                D.black = 4012352;
                D.dark = 5725031;
                D.medium = 7569526;
                D.light = 10064002;
                D.white = 14276035;
                D.smallSize = 18;
                D.normalSize = 20;
                D.uiFont = "ui_font";
                D.uiFontMono = "ui_font_mono"
            };
            var lb = function() {
                this.click = new Nc;
                U.call(this);
                this.set_buttonMode(!0);
                this.addEventListener("click", l(this, this.onClick));
                this.setSize(12, 12)
            };
            g["com.watabou.coogee.ui.elements.CloseButton"] = lb;
            lb.__name__ = "com.watabou.coogee.ui.elements.CloseButton";
            lb.__super__ = U;
            lb.prototype = v(U.prototype, {
                layout: function() {
                    this.get_graphics().clear();
                    this.get_graphics().beginFill(D.white);
                    this.get_graphics().drawCircle(this.rWidth / 2, this.rHeight / 2, 6)
                },
                onClick: function(a) {
                    a.stopPropagation();
                    this.click.dispatch()
                },
                __class__: lb
            });
            var Va = function(a) {
                null == a && (a = 0);
                this.click = new Nc;
                U.call(this);
                this.bg = new ta(0, 0);
                this.addChild(this.bg);
                0 == a && (a = D.black);
                var b = D.smallSize / 4;
                this.image = new ka;
                var c = this.image.get_graphics();
                c.beginFill(a);
                c.moveTo(-b, -b);
                c.lineTo(b, -b);
                c.lineTo(0, b);
                this.addChild(this.image);
                this.set_buttonMode(!0);
                this.addEventListener("click", l(this, this.onClick));
                this.setSize(2 * b, 2 * b)
            };
            g["com.watabou.coogee.ui.elements.DropDownButton"] = Va;
            Va.__name__ = "com.watabou.coogee.ui.elements.DropDownButton";
            Va.__super__ = U;
            Va.prototype = v(U.prototype, {
                layout: function() {
                    this.bg.setSize(this.rWidth, this.rHeight);
                    this.image.set_x(this.rWidth / 2);
                    this.image.set_y(this.rHeight / 2)
                },
                onClick: function(a) {
                    a.stopPropagation();
                    this.click.dispatch()
                },
                __class__: Va
            });
            var eb = function(a, b, c, d) {
                null == d && (d = 0);
                null == c && (c = 1E3);
                null == b && (b = 0);
                var f = this;
                if (0 == d) tc.call(this, null == c ? "null" : "" + c, !0);
                else {
                    for (var h = "0.", k = 0, n = d; k < n;) k++, h += "0";
                    tc.call(this, h, !0)
                }
                this.min = b;
                this.max = c;
                this.set_text(null == a ? "null" : "" + a);
                this.set_restrict("-0-9.");
                var p = Math.pow(10, Math.floor(Math.log(c - b) / Math.log(10)) - 1),
                    P = 0 < d ? Math.pow(10, d) : 0;
                this.addEventListener("mouseWheel", function(a) {
                    var d = parseFloat(f.get_text());
                    d = Fc.gate(d + (0 < a.delta ? 1 : -1) * p, b, c);
                    0 < P && (d = Math.round(d * P) / P);
                    f.set_text(null == d ? "null" : "" + d)
                })
            };
            g["com.watabou.coogee.ui.elements.FloatInput"] = eb;
            eb.__name__ = "com.watabou.coogee.ui.elements.FloatInput";
            eb.__super__ = tc;
            eb.prototype = v(tc.prototype, {
                __class__: eb
            });
            var Ef = function(a) {
                this.click = new ec;
                var b = this;
                U.call(this);
                this.bg = ta.light();
                this.add(this.bg);
                this.hbox = new ed;
                this.add(this.hbox);
                for (var c = 0; c < a.length;) {
                    var d = [a[c]];
                    ++c;
                    var f = new fb(d[0]);
                    f.click.add(function(a) {
                        return function() {
                            b.click.dispatch(a[0])
                        }
                    }(d));
                    f.set_width(100);
                    this.hbox.add(f)
                }
                this.setSize(this.hbox.get_width(), this.hbox.get_height())
            };
            g["com.watabou.coogee.ui.elements.FormButtons"] = Ef;
            Ef.__name__ = "com.watabou.coogee.ui.elements.FormButtons";
            Ef.__super__ = U;
            Ef.prototype = v(U.prototype, {
                layout: function() {
                    this.rWidth = Math.max(this.rWidth, this.hbox.get_width());
                    this.bg.set_width(this.rWidth);
                    this.bg.set_height(this.rHeight);
                    this.hbox.set_x(this.rWidth - this.hbox.get_width());
                    this.hbox.set_y((this.rHeight - this.hbox.get_height()) / 2 | 0)
                },
                __class__: Ef
            });
            var Od = function(a, b, c, d) {
                null == d && (d = 0);
                null == c && (c = 1E3);
                null == b && (b = 0);
                var f = this,
                    h = "0";
                if (0 == d)
                    for (var k = c; 10 <= k;) h += "0", k = k / 10 | 0;
                else
                    for (k = 1; k < d;) k++, h += "0";
                tc.call(this, h, !0);
                this.min = b;
                this.max = c;
                this.set_text(null == a ? "null" : "" + a);
                this.set_restrict("0-9");
                this.addEventListener("mouseWheel", function(a) {
                    a =
                        0 < a.delta ? 1 : -1;
                    f.update.dispatch(H.string(f.set_value(f.get_value() + a)))
                })
            };
            g["com.watabou.coogee.ui.elements.IntInput"] = Od;
            Od.__name__ = "com.watabou.coogee.ui.elements.IntInput";
            Od.__super__ = tc;
            Od.prototype = v(tc.prototype, {
                get_value: function() {
                    return Fc.gatei(H.parseInt(this.get_text()), this.min, this.max)
                },
                set_value: function(a) {
                    a = Fc.gatei(a, this.min, this.max);
                    this.tf.set_text(null == a ? "null" : "" + a);
                    return a
                },
                __class__: Od,
                __properties__: v(tc.prototype.__properties__, {
                    set_value: "set_value",
                    get_value: "get_value"
                })
            });
            var Oc = function(a, b) {
                U.call(this);
                this.border2 = ta.black();
                this.add(this.border2);
                this.paint2 = ta.white();
                this.add(this.paint2);
                this.border = ta.black();
                this.add(this.border);
                this.paint = ta.white();
                this.add(this.paint);
                this.colors = a;
                a = this.colors[0];
                this.paint.bmp.get_bitmapData().setPixel(0, 0, a);
                a = 1 < this.colors.length ? this.colors[1] : D.white;
                this.paint2.bmp.get_bitmapData().setPixel(0, 0, a);
                this.setSize(b, b);
                this.addEventListener("click", l(this, this.onClickEvent))
            };
            g["com.watabou.coogee.ui.elements.MultiSwatch"] =
                Oc;
            Oc.__name__ = "com.watabou.coogee.ui.elements.MultiSwatch";
            Oc.__super__ = U;
            Oc.prototype = v(U.prototype, {
                layout: function() {
                    var a = this.rWidth - 4 - 1,
                        b = this.rHeight - 4 - 1;
                    this.border.setSize(a, b);
                    this.paint.setSize(a - 2, b - 2);
                    this.paint.set_x(this.paint.set_y(1));
                    this.border2.setSize(a, b);
                    this.border2.set_x(this.border2.set_y(5));
                    this.paint2.setSize(a - 2, b - 2);
                    this.paint2.set_x(this.border2.get_x() + 1);
                    this.paint2.set_y(this.border2.get_y() + 1)
                },
                onClickEvent: function(a) {
                    null != this.callback && this.callback()
                },
                __class__: Oc
            });
            var vd = function(a, b) {
                U.call(this);
                this.border = ta.black();
                this.add(this.border);
                this.paint = new ta(a);
                this.add(this.paint);
                this.setSize(b, b);
                this.addEventListener("click", l(this, this.onClickEvent))
            };
            g["com.watabou.coogee.ui.elements.Swatch"] = vd;
            vd.__name__ = "com.watabou.coogee.ui.elements.Swatch";
            vd.__super__ = U;
            vd.prototype = v(U.prototype, {
                layout: function() {
                    this.border.setSize(this.rWidth, this.rHeight);
                    this.paint.setSize(this.rWidth - 2, this.rHeight - 2);
                    this.paint.set_x(this.paint.set_y(1))
                },
                onClick: function(a) {
                    this.callback =
                        a;
                    this.set_buttonMode(null != a)
                },
                onClickEvent: function(a) {
                    null != this.callback && this.callback()
                },
                __class__: vd
            });
            var fd = function(a, b, c, d) {
                null == c && (c = !1);
                this.update = new ec;
                this.edit = function(a) {
                    hb.trace(a.get_value(), {
                        fileName: "com/watabou/coogee/ui/elements/TextView.hx",
                        lineNumber: 22,
                        className: "com.watabou.coogee.ui.elements.TextView",
                        methodName: "edit"
                    })
                };
                this.data2text = function(a) {
                    return H.string(a)
                };
                this._value = null;
                var f = this;
                null != b && (this.data2text = b);
                null != d && (this.edit = d);
                this._value = a;
                U.call(this);
                this.border = ta.black();
                this.add(this.border);
                this.bg = ta.white();
                this.bg.set_x(this.bg.set_y(2));
                this.add(this.bg);
                this.tf = ld.get(this.data2text(a), D.format(D.uiFont, c ? D.smallSize : D.normalSize, D.black));
                this.addChild(this.tf);
                this.btn = new Va;
                this.btn.set_width(D.normalSize);
                this.btn.set_enabled(!1);
                this.add(this.btn);
                this.tf.mouseEnabled = !1;
                this.set_buttonMode(!0);
                this.addEventListener("click", function(a) {
                    f.edit(f)
                });
                this.setSize(Math.ceil(this.tf.get_width() + this.btn.get_width()), Math.ceil(this.tf.get_height()))
            };
            g["com.watabou.coogee.ui.elements.TextView"] = fd;
            fd.__name__ = "com.watabou.coogee.ui.elements.TextView";
            fd.editInForm = function(a, b, c) {
                return function(d) {
                    var f = u.findForm(a);
                    null == f && (f = w.createInstance(a, []), u.showDialog(f));
                    f.link(b, d.get_value(), function(a) {
                        d.set_value(a);
                        d.update.dispatch(a)
                    }, c)
                }
            };
            fd.__super__ = U;
            fd.prototype = v(U.prototype, {
                layout: function() {
                    this.border.setSize(this.rWidth, this.rHeight);
                    this.bg.setSize(this.rWidth - 4, this.rHeight - 4);
                    this.tf.set_autoSize(2);
                    this.tf.set_width(this.rWidth -
                        this.btn.get_width());
                    this.tf.set_height(this.rHeight);
                    this.tf.set_scrollH(0);
                    this.btn.set_height(this.rHeight);
                    this.btn.set_x(this.rWidth - this.btn.get_width());
                    this.btn.set_y((this.rHeight - this.btn.get_height()) / 2)
                },
                get_value: function() {
                    return this._value
                },
                set_value: function(a) {
                    this._value = a;
                    this.tf.set_text(this.data2text(a));
                    return a
                },
                __class__: fd,
                __properties__: v(U.prototype.__properties__, {
                    set_value: "set_value",
                    get_value: "get_value"
                })
            });
            var xc = function(a) {
                ic.call(this, null != a ? a.concat(xc.okCancel) :
                    xc.okCancel)
            };
            g["com.watabou.coogee.ui.elements.EditForm"] = xc;
            xc.__name__ = "com.watabou.coogee.ui.elements.EditForm";
            xc.__super__ = ic;
            xc.prototype = v(ic.prototype, {
                getTitle: function() {
                    return "Edit"
                },
                onButton: function(a) {
                    if ("OK" == a) {
                        if (null != this.onOK) this.onOK(this.get());
                        this.dialog.hide()
                    } else ic.prototype.onButton.call(this, a)
                },
                set: function(a) {},
                get: function() {
                    return null
                },
                link: function(a, b, c, d) {
                    null != a && this.dialog.setTitle(a);
                    this.set(b);
                    this.onOK = c;
                    null != this.host && this.host.dialog.onHide.remove(l(this,
                        this.onHostHidden));
                    this.host = d;
                    null != d && d.dialog.onHide.add(l(this, this.onHostHidden))
                },
                onHostHidden: function(a) {
                    this.dialog.hide()
                },
                __class__: xc
            });
            var Ff = function(a) {
                this.close = new Nc;
                var b = this;
                U.call(this);
                this.bg = ta.black();
                this.add(this.bg);
                this.btn = new lb;
                this.btn.click.add(function() {
                    b.close.dispatch()
                });
                this.add(this.btn);
                this.tf = ld.get(a, D.format(D.uiFont, D.smallSize, D.white));
                this.tf.mouseEnabled = !1;
                this.addChild(this.tf);
                this.setSize(this.tf.get_width(), 36)
            };
            g["com.watabou.coogee.ui.elements.WindowHeader"] =
                Ff;
            Ff.__name__ = "com.watabou.coogee.ui.elements.WindowHeader";
            Ff.__super__ = U;
            Ff.prototype = v(U.prototype, {
                layout: function() {
                    this.bg.setSize(this.rWidth, this.rHeight);
                    this.btn.setSize(this.rHeight, this.rHeight);
                    this.btn.set_x(this.rWidth - this.rHeight);
                    this.tf.set_x(this.tf.set_y((this.rHeight - this.tf.get_height()) / 2 | 0))
                },
                setText: function(a) {
                    this.tf.set_text(a)
                },
                __class__: Ff
            });
            var Le = function() {
                var a = this;
                xc.call(this);
                var b = new ed;
                this.swatch = new vd(0, 60);
                this.swatch.valign = "fill";
                b.add(this.swatch);
                var c = new Pd(3);
                c.setMargins(0, 10);
                var d = function(b, d) {
                    b = new Ib(b);
                    b.valign = "center";
                    c.add(b);
                    var f = new $d(0, d);
                    f.valign = "center";
                    c.add(f);
                    var h = new Od(0, 0, d, 3);
                    c.add(h);
                    h.update.add(function(a) {
                        f.set_value(h.get_value())
                    });
                    f.change.add(function(b) {
                        h.set_value(Math.round(b));
                        a.updateSwatch()
                    });
                    return f
                };
                this.hue = d("Hue", 359);
                this.hue.cycled = !0;
                this.sat = d("Sat", 100);
                this.val = d("Val", 100);
                b.add(c);
                this.add(b)
            };
            g["com.watabou.coogee.ui.forms.ColorForm"] = Le;
            Le.__name__ = "com.watabou.coogee.ui.forms.ColorForm";
            Le.__super__ = xc;
            Le.prototype = v(xc.prototype, {
                set: function(a) {
                    this.setColor(a)
                },
                get: function() {
                    return this.color
                },
                setColor: function(a) {
                    a = Gc.rgb2hsv(a);
                    this.hue.set_value(a.x);
                    this.sat.set_value(100 * a.y);
                    this.val.set_value(100 * a.z)
                },
                updateSwatch: function() {
                    var a = this.color = Gc.hsv(this.hue.get_value(), this.sat.get_value() / 100, this.val.get_value() / 100);
                    this.swatch.paint.bmp.get_bitmapData().setPixel(0, 0, a)
                },
                __class__: Le
            });
            var Id = function() {
                var a = this;
                xc.call(this);
                var b = new gb,
                    c = new ed;
                c.setMargins(0,
                    10);
                this.face = new tc("");
                this.face.set_width(300);
                this.face.set_prompt("Font name");
                this.face.update.add(function(b) {
                    a.updatePreview()
                });
                c.add(this.face);
                this.size = new Od(18, 8, 96);
                this.size.set_restrict("0-9");
                this.size.update.add(function(b) {
                    a.updatePreview()
                });
                c.add(this.size);
                var d = new ed;
                d.setMargins(0, 10);
                this.bold = new ud("Bold");
                this.bold.changed.add(function(b) {
                    a.updatePreview()
                });
                d.add(this.bold);
                this.italic = new ud("Italic");
                this.italic.changed.add(function(b) {
                    a.updatePreview()
                });
                d.add(this.italic);
                this.preview = new Qh;
                this.preview.halign = "fill";
                this.preview.set_height(100);
                b.add(c);
                b.add(d);
                b.add(this.preview);
                this.add(b)
            };
            g["com.watabou.coogee.ui.forms.FontForm"] = Id;
            Id.__name__ = "com.watabou.coogee.ui.forms.FontForm";
            Id.font2text = function(a) {
                if (null == a) return "Default";
                if (null != a.face) {
                    var b = a.face;
                    b = b.length <= Id.maxFaceLength ? b : N.substr(b, 0, Id.maxFaceLength - 1) + "..."
                } else null != a.embedded && ac.exists(a.embedded) ? (b = ac.getFont(a.embedded).name, b = "[" + (b.length <= Id.maxFaceLength ? b : N.substr(b, 0, Id.maxFaceLength -
                    1) + "...") + "]") : b = "[default]";
                b += " " + a.size;
                a.bold && (b += ", bold");
                a.italic && (b += ", italic");
                return b
            };
            Id.__super__ = xc;
            Id.prototype = v(xc.prototype, {
                set: function(a) {
                    this.face.set_text(null != a.face ? a.face : "");
                    this.size.set_value(a.size);
                    this.bold.set_value(a.bold);
                    this.italic.set_value(a.italic);
                    this.embedded = a.embedded;
                    this.face.set_prompt(null != this.embedded && ac.exists(this.embedded) ? ac.getFont(this.embedded).name : "Font name");
                    this.preview.setFont(a);
                    this.face.selecteAll();
                    null != this.stage && this.stage.set_focus(this.face)
                },
                get: function() {
                    return {
                        face: "" == this.face.get_text() ? null : this.face.get_text(),
                        embedded: this.embedded,
                        size: this.size.get_value(),
                        bold: this.bold.get_value(),
                        italic: this.italic.get_value()
                    }
                },
                updatePreview: function() {
                    this.preview.setFont(this.get())
                },
                __class__: Id
            });
            var Qh = function(a) {
                null == a && (a = "Sample Text");
                U.call(this);
                this.border = ta.black();
                this.addChild(this.border);
                this.bg = ta.white();
                this.bg.set_x(this.bg.set_y(1));
                this.addChild(this.bg);
                this.tf = new sc;
                this.tf.set_text(a);
                this.fixText();
                this.addChild(this.tf);
                this.maskRect = new md;
                this.addChild(this.maskRect);
                this.tf.set_mask(this.maskRect)
            };
            g["com.watabou.coogee.ui.forms._FontForm.FontPreview"] = Qh;
            Qh.__name__ = "com.watabou.coogee.ui.forms._FontForm.FontPreview";
            Qh.__super__ = U;
            Qh.prototype = v(U.prototype, {
                layout: function() {
                    var a = this.maskRect.get_graphics();
                    a.clear();
                    a.beginFill(16711680);
                    a.drawRect(0, 0, this.rWidth, this.rHeight);
                    this.border.set_width(this.rWidth);
                    this.border.set_height(this.rHeight);
                    this.bg.set_width(this.rWidth - 2);
                    this.bg.set_height(this.rHeight -
                        2);
                    this.tf.set_x(Math.max((this.rWidth - this.tf.get_width()) / 2, 0));
                    this.tf.set_y(Math.max((this.rHeight - this.tf.get_height()) / 2, 0))
                },
                setFont: function(a) {
                    a = Xc.font2format(a);
                    null == a && (a = new we);
                    a.color = D.black;
                    this.tf.setTextFormat(a);
                    this.fixText();
                    this.layout()
                },
                fixText: function() {
                    this.tf.set_autoSize(1);
                    var a = this.tf.get_width(),
                        b = this.tf.get_height();
                    this.tf.set_autoSize(2);
                    this.tf.set_width(Math.ceil(a));
                    this.tf.set_height(Math.ceil(b))
                },
                __class__: Qh
            });
            var Wj = function(a) {
                ic.call(this, ["OK"]);
                var b =
                    new bh;
                a = new Jb(a);
                a.mouseEnabled = !1;
                a.mouseChildren = !1;
                a.set_width(360);
                b.add(a);
                this.add(b)
            };
            g["com.watabou.coogee.ui.forms.Message"] = Wj;
            Wj.__name__ = "com.watabou.coogee.ui.forms.Message";
            Wj.__super__ = ic;
            Wj.prototype = v(ic.prototype, {
                getTitle: function() {
                    return "Message"
                },
                __class__: Wj
            });
            var Xj = function() {
                xc.call(this);
                this.content = new gb;
                this.content.setMargins(12, 12);
                this.add(this.content)
            };
            g["com.watabou.coogee.ui.forms.MultiColorForm"] = Xj;
            Xj.__name__ = "com.watabou.coogee.ui.forms.MultiColorForm";
            Xj.__super__ = xc;
            Xj.prototype = v(xc.prototype, {
                onButton: function(a) {
                    "Add" == a ? (a = this.get(), a.push(a[a.length - 1]), this.set(a)) : xc.prototype.onButton.call(this, a)
                },
                set: function(a) {
                    this.content.removeChildren();
                    for (var b = [], c = 0, d = a.length; c < d;) {
                        var f = c++,
                            h = a[f];
                        if (0 < f) {
                            var k = new ta(D.black);
                            k.halign = "fill";
                            k.setSize(2, 2);
                            this.content.add(k)
                        }
                        f = new Yj(this.content, h, 0 == f, f == a.length - 1);
                        f.action.add(l(this, this.onItemAction));
                        b.push(f)
                    }
                    this.items = b;
                    this.add(this.content);
                    va.__cast(this.parent, Hd).resize(!0)
                },
                get: function() {
                    for (var a = [], b = 0, c = this.items; b < c.length;) {
                        var d = c[b];
                        ++b;
                        a.push(d.color)
                    }
                    return a
                },
                onItemAction: function(a, b) {
                    var c = this.items.indexOf(a),
                        d = this.get();
                    d.splice(c, 1);
                    switch (b) {
                        case "Duplicate":
                            d.splice(c, 0, a.color);
                            d.splice(c, 0, a.color);
                            break;
                        case "Move down":
                            d.splice(c + 1, 0, a.color);
                            break;
                        case "Move up":
                            d.splice(c - 1, 0, a.color)
                    }
                    this.set(d)
                },
                __class__: Xj
            });
            var Yj = function(a, b, c, d) {
                this.action = new ah;
                var f = this;
                this.color = b;
                var h = new ed;
                h.setMargins(0, 10);
                var k = new gb;
                k.setMargins(0, 10);
                this.hex = new tc("#000000", !0);
                this.hex.set_text(O.hex(b, 6));
                this.hex.set_restrict("#0-9a-fA-F");
                this.hex.update.add(l(this, this.onHex));
                k.add(this.hex);
                this.swatch = new vd(b, 10 + 2 * this.hex.get_height());
                this.swatch.halign = "fill";
                k.add(this.swatch);
                h.add(k);
                k = new Pd(4);
                k.setMargins(0, 10);
                var n = ["Duplicate"];
                c || n.push("Move up");
                d || n.push("Move down");
                c && d || n.push("Delete");
                c = new fe("...", n);
                c.action.add(function(a) {
                    f.action.dispatch(f, a)
                });
                c.valign = "fill";
                b = Gc.rgb2hsv(b);
                this.hue = this.addRow(k, "Hue",
                    359, b.x, c);
                this.sat = this.addRow(k, "Sat", 100, 100 * b.y);
                this.val = this.addRow(k, "Val", 100, 100 * b.z);
                h.add(k);
                a.add(h)
            };
            g["com.watabou.coogee.ui.forms.ColorItem"] = Yj;
            Yj.__name__ = "com.watabou.coogee.ui.forms.ColorItem";
            Yj.prototype = {
                addRow: function(a, b, c, d, f) {
                    var h = this;
                    c |= 0;
                    d |= 0;
                    b = new Ib(b);
                    b.valign = "center";
                    a.add(b);
                    var k = new $d(0, c);
                    k.set_value(d);
                    k.valign = "center";
                    a.add(k);
                    var n = new Od(d, 0, c, 3);
                    a.add(n);
                    n.update.add(function(a) {
                        k.set_value(n.get_value())
                    });
                    k.change.add(function(a) {
                        n.set_value(Math.round(a));
                        h.updateSwatch()
                    });
                    null != f ? a.add(f) : a.addEmpty();
                    return k
                },
                onHex: function(a) {
                    "#" == a.charAt(0) && (a = N.substr(a, 1, null));
                    if (3 == a.length) {
                        var b = [];
                        b.push(a.charAt(0) + a.charAt(0));
                        b.push(a.charAt(1) + a.charAt(1));
                        b.push(a.charAt(2) + a.charAt(2));
                        a = b.join("")
                    }
                    6 < a.length && (a = N.substr(a, 0, 6));
                    a = H.parseInt("0x" + a);
                    a = Gc.rgb2hsv(a);
                    this.hue.set_value(a.x);
                    this.sat.set_value(100 * a.y);
                    this.val.set_value(100 * a.z);
                    this.updateSwatch()
                },
                updateSwatch: function() {
                    var a = this.color = Gc.hsv(this.hue.get_value(), this.sat.get_value() /
                        100, this.val.get_value() / 100);
                    this.swatch.paint.bmp.get_bitmapData().setPixel(0, 0, a);
                    this.hex.set_text(O.hex(this.color, 6))
                },
                __class__: Yj
            };
            var Hc = function(a, b) {
                this.onNullAsset = function(a, b) {
                    hb.trace("No " + a + " palette!", {
                        fileName: "com/watabou/coogee/ui/forms/PaletteForm.hx",
                        lineNumber: 38,
                        className: "com.watabou.coogee.ui.forms.PaletteForm",
                        methodName: "onNullAsset"
                    })
                };
                this.getName = function(a) {
                    return "palette"
                };
                var c = this;
                oc.call(this);
                this.form = new ed;
                this.form.setMargins(0, 0);
                this.add(this.form);
                this.tabs =
                    new ig;
                this.form.add(this.tabs);
                var d = [new fb("Load", l(this, this.onLoad)), new fb("Apply", function() {
                    a(c.getPalette())
                }), new fb("Save", function() {
                    c.onSave(c.getPalette())
                })];
                if (null != b) {
                    for (var f = [], h = []; 0 < b.length;) f.push(b.shift()), h.push(b.shift());
                    b = new fe("Preset", f, h);
                    b.action.add(function(a) {
                        c.loadPreset(a)
                    });
                    d.unshift(b)
                }
                this.buttons = new Rh(d);
                this.form.add(this.buttons);
                this.onApply = a;
                this.values = []
            };
            g["com.watabou.coogee.ui.forms.PaletteForm"] = Hc;
            Hc.__name__ = "com.watabou.coogee.ui.forms.PaletteForm";
            Hc.txt2color = function(a) {
                "#" == a.charAt(0) && (a = N.substr(a, 1, null));
                if (3 == a.length) {
                    var b = [];
                    b.push(a.charAt(0) + a.charAt(0));
                    b.push(a.charAt(1) + a.charAt(1));
                    b.push(a.charAt(2) + a.charAt(2));
                    a = b.join("")
                }
                6 < a.length && (a = N.substr(a, 0, 6));
                return H.parseInt("0x" + a)
            };
            Hc.txt2float = function(a, b, c) {
                return Fc.gate(parseFloat(a), b, c)
            };
            Hc.txt2int = function(a, b, c) {
                return Fc.gatei(H.parseInt(a), b | 0, c | 0)
            };
            Hc.swatches = function(a, b) {
                return function(c) {
                    for (var d = null != a ? a + "_" : "", f = [], h = 0; h < b.length;) {
                        var k = b[h];
                        ++h;
                        f.push(Sh.get(c.getColor(k)))
                    }
                    return d +
                        f.join("_")
                }
            };
            Hc.__super__ = oc;
            Hc.prototype = v(oc.prototype, {
                onShow: function() {
                    oc.prototype.onShow.call(this);
                    this.tabs.onTab(Hc.lastTab);
                    this.tabs.change.add(function(a) {
                        Hc.lastTab = a
                    })
                },
                onEnter: function() {
                    this.onApply(this.getPalette())
                },
                layout: function() {
                    null != this.tabs && this.tabs.layout();
                    this.form.layout();
                    oc.prototype.layout.call(this)
                },
                onKey: function(a) {
                    var b = this.tabs.getTab();
                    switch (a) {
                        case 33:
                            if (b < this.tabs.getTabCount() - 1) this.tabs.onTab(b + 1);
                            break;
                        case 34:
                            if (0 < b) this.tabs.onTab(this.tabs.getTab() -
                                1);
                            break;
                        default:
                            return oc.prototype.onKey.call(this, a)
                    }
                    return !0
                },
                addTab: function(a) {
                    this.grid = new Pd(2);
                    this.tabs.addTab(a, this.grid)
                },
                addColor: function(a, b, c) {
                    var d = this;
                    null == this.grid && this.addTab(null);
                    var f = new Ib(b);
                    this.grid.add(f);
                    b = new ed;
                    b.setMargins(0, 10);
                    var h = new tc("#000000", !0);
                    h.set_text(O.hex(c, 6));
                    h.set_restrict("#0-9a-fA-F");
                    b.add(h);
                    var k = new vd(c, h.get_height());
                    b.add(k);
                    h.update.add(function(a) {
                        a = Hc.txt2color(a);
                        k.paint.bmp.get_bitmapData().setPixel(0, 0, a)
                    });
                    k.onClick(function() {
                        d.onColor(f.get_text(),
                            k,
                            function(a) {
                                h.set_text(O.hex(a, 6));
                                k.paint.bmp.get_bitmapData().setPixel(0, 0, a)
                            })
                    });
                    this.grid.add(b);
                    this.values.push({
                        id: a,
                        type: jc.COLOR,
                        view: h,
                        swatch: k
                    });
                    this.layout()
                },
                addInt: function(a, b, c, d, f) {
                    null == this.grid && this.addTab(null);
                    b = new Ib(b);
                    this.grid.add(b);
                    c = new Od(c, d, f, 7);
                    this.grid.add(c);
                    this.values.push({
                        id: a,
                        type: jc.INT,
                        view: c,
                        min: d,
                        max: f
                    });
                    this.layout()
                },
                addEnum: function(a, b, c, d) {
                    null == this.grid && this.addTab(null);
                    b = new Ib(b);
                    this.grid.add(b);
                    c = Rc.ofStrings(c);
                    c.set_text(d);
                    c.halign =
                        "fill";
                    this.grid.add(c);
                    this.values.push({
                        id: a,
                        type: jc.STRING,
                        view: c
                    });
                    this.layout()
                },
                onColor: function(a, b, c) {
                    var d = u.findForm(Le);
                    null == d && (d = new Le, u.showDialog(d));
                    d.link(a, b.paint.bmp.get_bitmapData().getPixel(0, 0), c, this)
                },
                onLoad: function() {
                    var a = this,
                        b = new Gf;
                    b.addEventListener("select", function(c) {
                        b.addEventListener("complete", l(a, a.onPaletteLoaded));
                        b.load()
                    });
                    var c = [new Th("Palette", "*.json")];
                    b.browse(c)
                },
                loadPreset: function(a) {
                    if (ac.exists(a)) this.loadPalette(Xc.fromJSON(ac.getText(a)));
                    else this.onNullAsset(a, this)
                },
                onPaletteLoaded: function(a) {
                    try {
                        this.loadPalette(Xc.fromJSON(va.__cast(a.target, Gf).data.toString()))
                    } catch (b) {
                        q.show("Invalid palette file")
                    }
                },
                loadPalette: function(a) {
                    for (var b = 0, c = this.values; b < c.length;) {
                        var d = c[b];
                        ++b;
                        switch (d.type._hx_index) {
                            case 0:
                                var f = d.view,
                                    h = a.getColor(d.id, Hc.txt2color(f.get_text()));
                                d.swatch.paint.bmp.get_bitmapData().setPixel(0, 0, h);
                                f.set_text(O.hex(h, 6));
                                break;
                            case 1:
                                f = d.view;
                                h = a.getMulti(d.id, Hc.txt2color(f.get_text()));
                                d = d.multi;
                                d.colors =
                                    h;
                                var k = d.colors[0];
                                d.paint.bmp.get_bitmapData().setPixel(0, 0, k);
                                k = 1 < d.colors.length ? d.colors[1] : D.white;
                                d.paint2.bmp.get_bitmapData().setPixel(0, 0, k);
                                f.set_text(O.hex(h[0], 6));
                                break;
                            case 2:
                                f = d.view;
                                d = a.getFont(d.id, f.get_value());
                                f.set_value(d);
                                break;
                            case 3:
                                f = d.view;
                                d = a.getFloat(d.id, Hc.txt2float(f.get_text(), d.min, d.max));
                                f.set_text(null == d ? "null" : "" + d);
                                break;
                            case 4:
                                f = d.view;
                                d = a.getInt(d.id, Hc.txt2int(f.get_text(), d.min, d.max));
                                f.set_text(null == d ? "null" : "" + d);
                                break;
                            case 5:
                                d.view instanceof Rc ? (f =
                                    d.view, d = a.getString(d.id, f.get_value()), f.set_value(d)) : (f = d.view, d = a.getString(d.id, f.get_text()), f.set_text(d));
                                break;
                            case 6:
                                f = d.view, f.set_value(a.getBool(d.id, f.get_value()))
                        }
                    }
                },
                getPalette: function() {
                    for (var a = new Xc, b = 0, c = this.values; b < c.length;) {
                        var d = c[b];
                        ++b;
                        switch (d.type._hx_index) {
                            case 0:
                                var f = d.view,
                                    h = Hc.txt2color(f.get_text());
                                a.setColor(d.id, h);
                                f.set_text(O.hex(h, 6));
                                break;
                            case 1:
                                f = d.view;
                                h = d.multi.colors;
                                a.setMulti(d.id, h);
                                f.set_text(O.hex(h[0], 6));
                                break;
                            case 2:
                                f = d.view.get_value();
                                a.setFont(d.id, f);
                                break;
                            case 3:
                                f = d.view;
                                h = Hc.txt2float(f.get_text(), d.min, d.max);
                                a.setFloat(d.id, h);
                                f.set_text(null == h ? "null" : "" + h);
                                break;
                            case 4:
                                f = d.view;
                                h = Hc.txt2int(f.get_text(), d.min, d.max);
                                a.setInt(d.id, h);
                                f.set_text(null == h ? "null" : "" + h);
                                break;
                            case 5:
                                d.view instanceof Rc ? a.setString(d.id, d.view.get_value()) : a.setString(d.id, d.view.get_text());
                                break;
                            case 6:
                                a.setBool(d.id, d.view.get_value())
                        }
                    }
                    return a
                },
                onSave: function(a) {
                    ge.saveText(a.json(), this.getName(a) + ".json", "application/json")
                },
                __class__: Hc
            });
            var Rh = function(a) {
                U.call(this);
                this.bg = ta.light();
                this.add(this.bg);
                this.vbox = new gb;
                this.add(this.vbox);
                for (var b = 0; b < a.length;) {
                    var c = a[b];
                    ++b;
                    c.set_width(100);
                    this.vbox.add(c)
                }
                this.setSize(this.vbox.get_width(), this.vbox.get_height());
                this.valign = "fill"
            };
            g["com.watabou.coogee.ui.forms.ButtonColumn"] = Rh;
            Rh.__name__ = "com.watabou.coogee.ui.forms.ButtonColumn";
            Rh.__super__ = U;
            Rh.prototype = v(U.prototype, {
                layout: function() {
                    this.rWidth = Math.max(this.rWidth, this.vbox.get_width());
                    this.bg.set_width(this.rWidth);
                    this.bg.set_height(this.rHeight);
                    this.vbox.set_x((this.rWidth - this.vbox.get_width()) / 2 | 0);
                    this.vbox.set_y(0)
                },
                __class__: Rh
            });
            var Pd = function(a) {
                null == a && (a = 2);
                this.margin = this.gap = 10;
                U.call(this);
                this.cols = a
            };
            g["com.watabou.coogee.ui.layouts.Grid"] = Pd;
            Pd.__name__ = "com.watabou.coogee.ui.layouts.Grid";
            Pd.__super__ = U;
            Pd.prototype = v(U.prototype, {
                layout: function() {
                    for (var a = [], b = 0, c = this.cols; b < c;) b++, a.push(0);
                    c = a;
                    var d = [];
                    a = 0;
                    for (b = this.get_numChildren(); a < b;) {
                        var f = a++;
                        var h = f % this.cols,
                            k = f / this.cols |
                            0;
                        d.length <= k && d.push(0);
                        f = this.getChildAt(f);
                        var n = f.get_width();
                        c[h] < n && (c[h] = n);
                        h = f.get_height();
                        d[k] < h && (d[k] = h)
                    }
                    var p = n = this.margin;
                    a = k = h = 0;
                    for (b = this.get_numChildren(); a < b;) {
                        f = a++;
                        f = this.getChildAt(f);
                        var P = n,
                            g = p;
                        if (f instanceof U) {
                            var m = va.__cast(f, U);
                            switch (m.halign) {
                                case "center":
                                    P += (c[h] - m.get_width()) / 2;
                                    break;
                                case "fill":
                                    m.set_width(c[h]);
                                    break;
                                case "right":
                                    P += c[h] - m.get_width()
                            }
                            switch (m.valign) {
                                case "bottom":
                                    g += d[k] - m.get_height();
                                    break;
                                case "center":
                                    g += (d[k] - m.get_height()) / 2;
                                    break;
                                case "fill":
                                    m.set_height(d[k])
                            }
                        }
                        f.set_x(P);
                        f.set_y(g);
                        n += c[h] + this.gap;
                        ++h;
                        h == this.cols && (n = this.margin, p += d[k] + this.gap, ++k, h = 0)
                    }
                    this.rHeight = 2 * this.margin + (d.length - 1) * this.gap;
                    for (a = 0; a < d.length;) h = d[a], ++a, this.rHeight += h;
                    this.rWidth = 2 * this.margin + (c.length - 1) * this.gap;
                    for (a = 0; a < c.length;) n = c[a], ++a, this.rWidth += n
                },
                add: function(a) {
                    U.prototype.add.call(this, a);
                    this.layout()
                },
                addEmpty: function() {
                    this.add(new ta(0, 0))
                },
                setMargins: function(a, b) {
                    this.margin = a;
                    this.gap = b
                },
                __class__: Pd
            });
            var ed = function() {
                this.margin = this.gap = 10;
                U.call(this)
            };
            g["com.watabou.coogee.ui.layouts.HBox"] = ed;
            ed.__name__ = "com.watabou.coogee.ui.layouts.HBox";
            ed.__super__ = U;
            ed.prototype = v(U.prototype, {
                layout: function() {
                    for (var a = this.margin, b = 0, c = !1, d = 0, f = this.get_numChildren(); d < f;) {
                        var h = d++;
                        h = this.getChildAt(h);
                        "top" != h.valign && (c = !0);
                        "fill" != h.valign && b < h.get_height() && (b = h.get_height());
                        h.set_x(a);
                        h.set_y(this.margin);
                        a += h.get_width() + this.gap
                    }
                    if (c)
                        for (d = 0, f = this.get_numChildren(); d < f;) switch (h = d++, h = this.getChildAt(h), h.valign) {
                            case "bottom":
                                h.set_y(this.margin +
                                    (b - h.get_height()));
                                break;
                            case "center":
                                h.set_y(this.margin + (b - h.get_height()) / 2);
                                break;
                            case "fill":
                                h.set_height(b)
                        }
                    this.rWidth = a > this.margin ? a - this.gap + this.margin : 2 * this.margin;
                    this.rHeight = b + 2 * this.margin
                },
                add: function(a) {
                    U.prototype.add.call(this, a);
                    this.layout()
                },
                setMargins: function(a, b) {
                    this.margin = a;
                    this.gap = b
                },
                __class__: ed
            });
            var bh = function() {
                this.margin = 10;
                U.call(this)
            };
            g["com.watabou.coogee.ui.layouts.SimpleBox"] = bh;
            bh.__name__ = "com.watabou.coogee.ui.layouts.SimpleBox";
            bh.__super__ = U;
            bh.prototype =
                v(U.prototype, {
                    layout: function() {
                        for (var a = 0, b = 0, c = 0, d = this.get_numChildren(); c < d;) {
                            var f = c++;
                            f = this.getChildAt(f);
                            a < f.get_width() && (a = f.get_width());
                            b < f.get_height() && (b = f.get_height());
                            f.set_x(this.margin);
                            f.set_y(this.margin)
                        }
                        this.rWidth = a + 2 * this.margin;
                        this.rHeight = b + 2 * this.margin
                    },
                    add: function(a) {
                        U.prototype.add.call(this, a);
                        this.layout()
                    },
                    setMargins: function(a) {
                        this.margin = a
                    },
                    __class__: bh
                });
            var ig = function() {
                this.change = new ec;
                gb.call(this);
                this.setMargins(0, 0);
                this.tabRow = new Uh;
                this.tabRow.click.add(l(this,
                    this.onTab));
                this.tabRow.halign = "fill";
                this.add(this.tabRow);
                this.stack = new bh;
                this.stack.setMargins(0);
                this.add(this.stack)
            };
            g["com.watabou.coogee.ui.layouts.Tabs"] = ig;
            ig.__name__ = "com.watabou.coogee.ui.layouts.Tabs";
            ig.__super__ = gb;
            ig.prototype = v(gb.prototype, {
                layout: function() {
                    null != this.stack && this.stack.layout();
                    gb.prototype.layout.call(this)
                },
                addTab: function(a, b) {
                    null != a && this.tabRow.addTab(a);
                    this.stack.add(b);
                    this.tabRow.get_selected() != this.stack.get_numChildren() - 1 && null != a && b.set_visible(!1);
                    this.layout()
                },
                onTab: function(a) {
                    var b = this.tabRow.get_selected(); - 1 != b && this.stack.getChildAt(b).set_visible(!1);
                    this.stack.getChildAt(a).set_visible(!0);
                    this.tabRow.set_selected(a);
                    this.change.dispatch(a)
                },
                getTab: function() {
                    return this.tabRow.get_selected()
                },
                getTabCount: function() {
                    return this.tabRow.get_size()
                },
                set_rowSize: function(a) {
                    return this.tabRow.rowSize = a
                },
                __class__: ig,
                __properties__: v(gb.prototype.__properties__, {
                    set_rowSize: "set_rowSize"
                })
            });
            var ae = function() {
                this._selected = -1;
                this.tabs = [];
                this.rowSize = 256;
                this.click = new ec;
                U.call(this);
                this.bg = ta.light();
                this.add(this.bg)
            };
            g["com.watabou.coogee.ui.layouts._Tabs.TabButtons"] = ae;
            ae.__name__ = "com.watabou.coogee.ui.layouts._Tabs.TabButtons";
            ae.__super__ = U;
            ae.prototype = v(U.prototype, {
                get_selected: function() {
                    return this._selected
                },
                set_selected: function(a) {
                    return this._selected = a
                },
                get_size: function() {
                    return this.tabs.length
                },
                layout: function() {
                    this.bg.setSize(this.rWidth, this.rHeight)
                },
                addTab: function(a) {
                    a = new Vh(a);
                    a.click.add(l(this,
                        this.onTab));
                    this.tabs.push(a);
                    return a
                },
                onTab: function(a) {
                    this.click.dispatch(this.tabs.indexOf(a))
                },
                __class__: ae,
                __properties__: v(U.prototype.__properties__, {
                    get_size: "get_size",
                    set_selected: "set_selected",
                    get_selected: "get_selected"
                })
            });
            var Zj = function() {
                this.first = 0;
                ae.call(this);
                this.stripe = new ed;
                this.stripe.setMargins(0, 0);
                this.add(this.stripe);
                this.more = new fb("...");
                this.more.click.add(l(this, this.showList));
                this.more.set_visible(!1);
                this.add(this.more)
            };
            g["com.watabou.coogee.ui.layouts._Tabs.TabRow"] =
                Zj;
            Zj.__name__ = "com.watabou.coogee.ui.layouts._Tabs.TabRow";
            Zj.__super__ = ae;
            Zj.prototype = v(ae.prototype, {
                layout: function() {
                    ae.prototype.layout.call(this);
                    this.more.set_height(this.rHeight - 8);
                    this.more.set_x(this.rWidth - this.more.get_width());
                    this.more.set_y(4)
                },
                addTab: function(a) {
                    a = ae.prototype.addTab.call(this, a);
                    this.stripe.add(a);
                    this.updateSize();
                    this.layout();
                    1 == this.tabs.length && this.set_selected(0);
                    return a
                },
                set_selected: function(a) {
                    if (0 == this.tabs.length) return 0; - 1 != this._selected && this.tabs[this._selected].set_selected(!1);
                    this._selected = a;
                    if (this.tabs.length > this.rowSize) {
                        this.first > a ? this.first = a : a >= this.first + this.rowSize && (this.first = a - this.rowSize + 1);
                        for (var b = 0, c = this.tabs.length; b < c;) a = b++, this.tabs[a].set_visible(a >= this.first && a < this.first + this.rowSize);
                        this.stripe.set_x(-this.tabs[this.first].get_x())
                    } - 1 != this._selected && this.tabs[this._selected].set_selected(!0);
                    return this.get_selected()
                },
                updateSize: function() {
                    if (this.tabs.length <= this.rowSize) this.more.set_visible(!1), this.rWidth = this.stripe.get_width();
                    else {
                        this.more.set_visible(!0);
                        for (var a = this.rWidth = 0, b = this.tabs.length - this.rowSize + 1; a < b;) {
                            for (var c = a++, d = 0, f = 0, h = this.rowSize; f < h;) {
                                var k = f++;
                                d += this.tabs[c + k].get_width()
                            }
                            this.rWidth < d && (this.rWidth = d)
                        }
                        this.rWidth += 4 + this.more.get_width()
                    }
                    this.rHeight = this.stripe.get_height()
                },
                showList: function() {
                    for (var a = this, b = new dd, c = 0, d = this.tabs.length; c < d;) {
                        var f = [c++];
                        b.addItem(this.tabs[f[0]].get_text(), function(b) {
                            return function() {
                                a.click.dispatch(b[0])
                            }
                        }(f), this.get_selected() == f[0])
                    }
                    c = u.getRect(this.more);
                    u.showMenuAt(b, Math.ceil(c.x), c.get_bottom())
                },
                __class__: Zj
            });
            var Uh = function() {
                ae.call(this);
                this.stripes = new gb;
                this.stripes.setMargins(0, 0);
                this.add(this.stripes)
            };
            g["com.watabou.coogee.ui.layouts._Tabs.TabMultiRow"] = Uh;
            Uh.__name__ = "com.watabou.coogee.ui.layouts._Tabs.TabMultiRow";
            Uh.__super__ = ae;
            Uh.prototype = v(ae.prototype, {
                addTab: function(a) {
                    a = ae.prototype.addTab.call(this, a);
                    null == this.lastRow && (this.lastRow = new ed, this.lastRow.setMargins(0, 0), this.stripes.add(this.lastRow));
                    this.lastRow.add(a);
                    this.lastRow.get_numChildren() >= this.rowSize && (this.lastRow = null);
                    this.stripes.layout();
                    this.updateSize();
                    this.layout();
                    1 == this.tabs.length && this.set_selected(0);
                    return a
                },
                set_selected: function(a) {
                    if (0 == this.tabs.length) return 0; - 1 != this._selected && this.tabs[this._selected].set_selected(!1);
                    this._selected = a;
                    if (-1 != this._selected) {
                        a = this.tabs[this._selected];
                        a.set_selected(!0);
                        for (var b = this.stripes.getChildAt(this.stripes.get_numChildren() - 1); - 1 == b.getChildIndex(a);) b = this.stripes.removeChildAt(0),
                            this.stripes.add(b)
                    }
                    return this.get_selected()
                },
                updateSize: function() {
                    this.rWidth = this.stripes.get_width();
                    this.rHeight = this.stripes.get_height()
                },
                __class__: Uh
            });
            var Vh = function(a) {
                this.click = new ec;
                U.call(this);
                this.bg = new ka;
                this.addChild(this.bg);
                this.tf = ld.get("", D.format(D.uiFont, D.smallSize, D.black));
                this.tf.set_x(4);
                this.tf.set_y(8);
                this.addChild(this.tf);
                this.set_text(a);
                this.set_selected(!1);
                this.set_buttonMode(!0);
                this.addEventListener("click", l(this, this.onClick))
            };
            g["com.watabou.coogee.ui.layouts.Tab"] =
                Vh;
            Vh.__name__ = "com.watabou.coogee.ui.layouts.Tab";
            Vh.__super__ = U;
            Vh.prototype = v(U.prototype, {
                layout: function() {
                    var a = this.bg.get_graphics();
                    a.clear();
                    a.beginFill(D.white);
                    a.drawRoundRectComplex(0, 4, this.rWidth, this.rHeight - 4, 4, 4, 0, 0)
                },
                onClick: function(a) {
                    this.click.dispatch(this)
                },
                get_text: function() {
                    return this.tf.get_text()
                },
                set_text: function(a) {
                    this.tf.set_text(a);
                    this.setSize(this.tf.get_width() + 8, Math.ceil(this.tf.get_height() + 8));
                    return a
                },
                set_selected: function(a) {
                    this.mouseEnabled = !a;
                    this.bg.set_alpha(a ?
                        1 : 0);
                    return a
                },
                __class__: Vh,
                __properties__: v(U.prototype.__properties__, {
                    set_selected: "set_selected",
                    set_text: "set_text",
                    get_text: "get_text"
                })
            });
            var ld = function() {};
            g["com.watabou.coogee.ui.utils.Text"] = ld;
            ld.__name__ = "com.watabou.coogee.ui.utils.Text";
            ld.get = function(a, b, c, d) {
                null == a && (a = "");
                var f = new sc;
                null != c || null != d ? ld.activate(f, c, d) : f.set_selectable(!1);
                f.set_autoSize(1);
                null != b && f.set_defaultTextFormat(b);
                f.set_htmlText(a);
                return f
            };
            ld.input = function(a, b, c) {
                null == a && (a = "");
                var d = new sc;
                d.set_type(1);
                d.set_borderColor(d.get_defaultTextFormat().color);
                d.set_background(!0);
                d.set_border(!0);
                d.addEventListener("change", function(a) {
                    null != c && c()
                });
                d.set_defaultTextFormat(b);
                d.set_text("" != a ? a : " ");
                d.set_autoSize(1);
                d.set_height(d.get_height());
                d.set_autoSize(2);
                d.set_text(a);
                return d
            };
            ld.activate = function(a, b, c) {
                a.set_type(1);
                a.addEventListener("focusIn", function(b) {
                    a.set_borderColor(a.get_defaultTextFormat().color);
                    a.set_border(!0)
                });
                a.addEventListener("focusOut", function(b) {
                    a.set_border(!1);
                    null !=
                        c && c()
                });
                a.addEventListener("keyDown", function(b) {
                    if (13 == b.keyCode || 27 == b.keyCode) a.stage.set_focus(a.stage), b.stopPropagation()
                });
                a.addEventListener("change", function(a) {
                    null != b && b()
                })
            };
            var Wa = function(a, b) {
                this.type = a;
                null != b ? (a = new Qa, a.h.id = b, b = a) : b = new Qa;
                this.props = b;
                this.coords = [];
                this.items = []
            };
            g["com.watabou.formats.GeoJSON"] = Wa;
            Wa.__name__ = "com.watabou.formats.GeoJSON";
            Wa.lineString = function(a, b) {
                a = new Wa("LineString", a);
                a.coords = [
                    [b]
                ];
                return a
            };
            Wa.polygon = function(a, b) {
                a = new Wa("Polygon",
                    a);
                a.coords = [
                    [b]
                ];
                return a
            };
            Wa.multiPoint = function(a, b) {
                a = new Wa("MultiPoint", a);
                a.coords = [
                    [b]
                ];
                return a
            };
            Wa.multiPolygon = function(a, b) {
                a = new Wa("MultiPolygon", a);
                for (var c = [], d = 0; d < b.length;) {
                    var f = b[d];
                    ++d;
                    c.push([f])
                }
                a.coords = c;
                return a
            };
            Wa.geometryCollection = function(a, b) {
                a = new Wa("GeometryCollection", a);
                a.items = b;
                return a
            };
            Wa.featureCollection = function(a, b) {
                var c = new Wa("FeatureCollection");
                c.items = a;
                c.props = b;
                return c
            };
            Wa.feature = function(a, b) {
                var c = new Wa("Feature");
                c.items = null != a ? [a] : [];
                c.props = b;
                return c
            };
            Wa.replacer = function(a, b) {
                if (b instanceof Wa) {
                    var c = {
                        type: b.type
                    };
                    if (null != b.props)
                        for (var d = b.props.h, f = Object.keys(d), h = f.length, k = 0; k < h;) a = f[k++], c[a] = d[a];
                    switch (b.type) {
                        case "Feature":
                            0 < b.items.length && (c.geometry = b.items[0]);
                            break;
                        case "FeatureCollection":
                            c.features = b.items;
                            break;
                        case "GeometryCollection":
                            c.geometries = b.items;
                            break;
                        case "LineString":
                            c.coordinates = Wa.arrPoly(b.coords[0][0]);
                            break;
                        case "MultiLineString":
                            a = [];
                            d = 0;
                            for (b = b.coords[0]; d < b.length;) f = b[d], ++d, a.push(Wa.arrPoly(f));
                            c.coordinates = a;
                            break;
                        case "MultiPoint":
                            c.coordinates = Wa.arrPoly(b.coords[0][0]);
                            break;
                        case "MultiPolygon":
                            a = [];
                            d = 0;
                            for (b = b.coords; d < b.length;) {
                                f = b[d];
                                ++d;
                                h = [];
                                for (k = 0; k < f.length;) {
                                    var n = f[k];
                                    ++k;
                                    h.push(Wa.arrPoly(n))
                                }
                                a.push(h)
                            }
                            c.coordinates = a;
                            break;
                        case "Polygon":
                            a = [];
                            d = 0;
                            for (b = b.coords[0]; d < b.length;) n = b[d], ++d, a.push(Wa.arrPoly(n));
                            c.coordinates = a
                    }
                    return c
                }
                return b
            };
            Wa.arrPoly = function(a) {
                for (var b = [], c = 0; c < a.length;) {
                    var d = a[c];
                    ++c;
                    b.push(Wa.arrPoint(d))
                }
                return b
            };
            Wa.arrPoint = function(a) {
                return [Math.round(1E3 *
                    (Wa.CX + a.x * Wa.SCALE)) / 1E3, Math.round(1E3 * (Wa.CY - a.y * Wa.SCALE)) / 1E3]
            };
            Wa.prototype = {
                stringify: function() {
                    return JSON.stringify(this, Wa.replacer, "  ")
                },
                __class__: Wa
            };
            var I = function(a, b) {
                null == b && (b = 0);
                null == a && (a = 0);
                this.x = a;
                this.y = b
            };
            g["openfl.geom.Point"] = I;
            I.__name__ = "openfl.geom.Point";
            I.distance = function(a, b) {
                var c = a.x - b.x;
                a = a.y - b.y;
                return Math.sqrt(c * c + a * a)
            };
            I.polar = function(a, b) {
                return new I(a * Math.cos(b), a * Math.sin(b))
            };
            I.prototype = {
                add: function(a) {
                    return new I(a.x + this.x, a.y + this.y)
                },
                clone: function() {
                    return new I(this.x,
                        this.y)
                },
                copyFrom: function(a) {
                    this.x = a.x;
                    this.y = a.y
                },
                normalize: function(a) {
                    if (0 != this.x || 0 != this.y) a /= Math.sqrt(this.x * this.x + this.y * this.y), this.x *= a, this.y *= a
                },
                offset: function(a, b) {
                    this.x += a;
                    this.y += b
                },
                setTo: function(a, b) {
                    this.x = a;
                    this.y = b
                },
                subtract: function(a) {
                    return new I(this.x - a.x, this.y - a.y)
                },
                __toLimeVector2: function() {
                    null == I.__limeVector2 && (I.__limeVector2 = new jg);
                    var a = I.__limeVector2;
                    a.x = this.x;
                    a.y = this.y;
                    return I.__limeVector2
                },
                get_length: function() {
                    return Math.sqrt(this.x * this.x + this.y *
                        this.y)
                },
                __class__: I,
                __properties__: {
                    get_length: "get_length"
                }
            };
            var Oa = function(a, b) {
                this.width = a;
                this.height = b;
                this.root = W.createElement("svg");
                this.root.set("width", H.string(Math.round(1E3 * a) / 1E3));
                this.root.set("height", H.string(Math.round(1E3 * b) / 1E3));
                this.root.set("xmlns", "http://www.w3.org/2000/svg")
            };
            g["com.watabou.formats.SVG"] = Oa;
            Oa.__name__ = "com.watabou.formats.SVG";
            Oa.setOpacity = function(a, b) {
                a.set("opacity", null == b ? "null" : "" + b);
                return a
            };
            Oa.setFill = function(a, b, c) {
                null == c && (c = 1);
                a.set("fill",
                    "#" + O.hex(b, 6));
                1 > c && a.set("fill-opacity", null == c ? "null" : "" + c);
                return a
            };
            Oa.noFill = function(a) {
                a.set("fill", "none");
                return a
            };
            Oa.fillRule = function(a, b) {
                a.set("fill-rule", b);
                return a
            };
            Oa.setStroke = function(a, b, c, d, f) {
                null == c && (c = 0);
                a.set("stroke", "#" + O.hex(b, 6));
                0 != c && a.set("stroke-width", H.string(Math.round(1E3 * c) / 1E3));
                Oa.joinNcap(a, d, f);
                return a
            };
            Oa.strokeOpacity = function(a, b) {
                a.set("stroke-opacity", null == b ? "null" : "" + b);
                return a
            };
            Oa.joinNcap = function(a, b, c) {
                null != b && a.set("stroke-linejoin", b);
                null !=
                    c && a.set("stroke-linecap", c);
                return a
            };
            Oa.style = function(a, b) {
                a.set("style", b);
                return a
            };
            Oa.clearTransform = function(a) {
                a.remove("transform");
                return a
            };
            Oa.addTransform = function(a, b) {
                var c = a.get("transform");
                a.set("transform", null == c ? b : "" + b + " " + c);
                return a
            };
            Oa.translate = function(a, b, c) {
                return Oa.addTransform(a, "translate(" + H.string(Math.round(1E3 * b) / 1E3) + " " + H.string(Math.round(1E3 * c) / 1E3) + ")")
            };
            Oa.scale = function(a, b, c) {
                return Oa.addTransform(a, "scale(" + H.string(Math.round(1E3 * b) / 1E3) + " " + H.string(Math.round(1E3 *
                    c) / 1E3) + ")")
            };
            Oa.rotate = function(a, b, c, d) {
                null == d && (d = 0);
                null == c && (c = 0);
                return Oa.addTransform(a, "rotate(" + H.string(Math.round(1E3 * b) / 1E3) + " " + H.string(Math.round(1E3 * c) / 1E3) + " " + H.string(Math.round(1E3 * d) / 1E3) + ")")
            };
            Oa.x = function(a, b) {
                a.set("x", H.string(Math.round(1E3 * b) / 1E3))
            };
            Oa.y = function(a, b) {
                a.set("y", H.string(Math.round(1E3 * b) / 1E3))
            };
            Oa.linearGradient = function(a, b, c) {
                null == b && (b = "userSpaceOnUse");
                var d = W.createElement("linearGradient");
                d.set("id", a);
                d.set("gradientUnits", b);
                null != c && (a = c.transformPoint(Oa._p0),
                    c = c.transformPoint(Oa._p1), d.set("x1", H.string(Math.round(1E3 * a.x) / 1E3)), d.set("y1", H.string(Math.round(1E3 * a.y) / 1E3)), d.set("x2", H.string(Math.round(1E3 * c.x) / 1E3)), d.set("y2", H.string(Math.round(1E3 * c.y) / 1E3)));
                return d
            };
            Oa.radialGradient = function(a, b, c) {
                null == b && (b = "userSpaceOnUse");
                var d = W.createElement("radialGradient");
                d.set("id", a);
                d.set("gradientUnits", b);
                null != c && (d.set("cx", H.string(Math.round(1E3 * c.tx) / 1E3)), d.set("cy", H.string(Math.round(1E3 * c.ty) / 1E3)), d.set("r", H.string(Math.round(819200 *
                    c.a) / 1E3)));
                return d
            };
            Oa.stop = function(a, b, c) {
                null == c && (c = 1);
                var d = W.createElement("stop");
                d.set("offset", Math.round(100 * a) + "%");
                d.set("stop-color", "#" + O.hex(b, 6));
                1 > c && d.set("stop-opacity", null == c ? "null" : "" + c);
                return d
            };
            Oa.group = function(a) {
                var b = W.createElement("g");
                null != a && b.set("id", a);
                return b
            };
            Oa.clipPath = function(a) {
                var b = W.createElement("clipPath");
                b.set("id", a);
                Oa.setFill(b, 16777215);
                return b
            };
            Oa.text = function(a, b, c) {
                var d = W.createElement("text");
                a = W.createPCData(a);
                d.addChild(a);
                null !=
                    b && d.set("text-anchor", b);
                null != c && d.set("dominant-baseline", c);
                return d
            };
            Oa.tspan = function(a) {
                var b = W.createElement("tspan");
                a = W.createPCData(a);
                b.addChild(a);
                return b
            };
            Oa.rect = function(a, b, c, d) {
                var f = W.createElement("rect");
                f.set("x", H.string(Math.round(1E3 * a) / 1E3));
                f.set("y", H.string(Math.round(1E3 * b) / 1E3));
                f.set("width", H.string(Math.round(1E3 * c) / 1E3));
                f.set("height", H.string(Math.round(1E3 * d) / 1E3));
                return f
            };
            Oa.prototype = {
                __class__: Oa
            };
            var fc = function() {
                this.buff = new x
            };
            g["com.watabou.formats.SVGPath"] =
                fc;
            fc.__name__ = "com.watabou.formats.SVGPath";
            fc.prototype = {
                xml: function() {
                    var a = W.createElement("path");
                    a.set("d", this.buff.b);
                    return a
                },
                __class__: fc
            };
            var Ja = function() {};
            g["com.watabou.formats.Sprite2SVG"] = Ja;
            Ja.__name__ = "com.watabou.formats.Sprite2SVG";
            Ja.create = function(a, b, c, d) {
                null == d && (d = !0);
                null == c && (c = -1);
                d && (Ja.resetGradients(), Ja.resetImports());
                d = new Oa(a, b);
                Ja.defaultAttributes(d.root); - 1 != c && (a = Oa.rect(0, 0, a, b), Oa.setFill(a, c), d.root.addChild(a));
                return d
            };
            Ja.defaultAttributes = function(a) {
                Oa.joinNcap(a,
                    "round", "round");
                Oa.fillRule(a, "evenodd");
                Oa.noFill(a)
            };
            Ja.drawSprite = function(a) {
                var b = a.__isMask ? Oa.clipPath(a.get_name()) : Oa.group();
                Ja.copyAttributes(a, b);
                Ja.drawGraphics(a.get_graphics(), b);
                Ja.drawChildren(a, b);
                return b
            };
            Ja.drawShape = function(a) {
                var b = a.__isMask ? Oa.clipPath(a.get_name()) : Oa.group();
                Ja.copyAttributes(a, b);
                Ja.drawGraphics(a.get_graphics(), b);
                return b
            };
            Ja.copyAttributes = function(a, b) {
                1 == a.get_scaleX() && 1 == a.get_scaleY() || Oa.scale(b, a.get_scaleX(), a.get_scaleY());
                0 != a.get_rotation() &&
                    Oa.rotate(b, a.get_rotation());
                0 == a.get_x() && 0 == a.get_y() || Oa.translate(b, a.get_x(), a.get_y());
                1 > a.get_alpha() && Oa.setOpacity(b, a.get_alpha());
                if (10 != a.get_blendMode()) {
                    var c = Ja.BLEND_MODES,
                        d = a.get_blendMode();
                    Oa.style(b, "mix-blend-mode: " + c.h[d])
                }
                null != a.get_mask() && b.set("clip-path", "url(#" + a.get_mask().get_name() + ")")
            };
            Ja.drawGraphics = function(a, b) {
                var c = !1,
                    d = 0,
                    f = 1,
                    h = 1,
                    k = 2,
                    n = 1,
                    p = !1,
                    P = 0,
                    g = 1,
                    m = null;
                for (a = a.readGraphicsData(!1).iterator(); a.hasNext();) {
                    var q = a.next();
                    switch (q.__graphicsDataType) {
                        case 0:
                            c =
                                q;
                            f = c.thickness;
                            h = c.fill;
                            d = h.color;
                            h = h.alpha;
                            k = c.joints;
                            n = c.caps;
                            c = !0;
                            break;
                        case 1:
                            m = null;
                            p = q;
                            P = p.color;
                            g = p.alpha;
                            p = !0;
                            break;
                        case 2:
                            m = q;
                            Ja.gradients.push(m);
                            p = !0;
                            break;
                        case 3:
                            q = Ja.drawPath(q.commands, q.data, q.winding);
                            p && (null == m ? Oa.setFill(q, P, g) : q.set("fill", "url(#grad" + Ja.gradients.length + ")"));
                            c && (Oa.setStroke(q, d, f), 2 == k && 1 == n || Oa.joinNcap(q, Ja.JOINTS.h[k], Ja.CAPS.h[n]), 1 > h && Oa.strokeOpacity(q, h));
                            b.addChild(q);
                            break;
                        case 5:
                            p = c = !1, m = null
                    }
                }
            };
            Ja.drawPath = function(a, b, c) {
                c = new fc;
                var d = 0;
                a = a.iterator();
                a: for (; a.hasNext();) switch (a.next()) {
                    case 1:
                        var f = b.get(d),
                            h = fc.ly = b.get(d + 1);
                        c.buff.b += H.string(" M " + (H.string(Math.round(1E3 * (fc.lx = f)) / 1E3) + "," + H.string(Math.round(1E3 * h) / 1E3)));
                        d += 2;
                        break;
                    case 2:
                        f = b.get(d);
                        h = b.get(d + 1);
                        if (0 == c.buff.b.length) {
                            var k = fc.ly = fc.ly;
                            c.buff.b += H.string(" M " + (H.string(Math.round(1E3 * (fc.lx = fc.lx)) / 1E3) + "," + H.string(Math.round(1E3 * k) / 1E3)))
                        }
                        h = fc.ly = h;
                        c.buff.b += H.string(" L " + (H.string(Math.round(1E3 * (fc.lx = f)) / 1E3) + "," + H.string(Math.round(1E3 * h) / 1E3)));
                        d += 2;
                        break;
                    case 3:
                        k =
                            b.get(d);
                        var n = b.get(d + 1);
                        f = b.get(d + 2);
                        h = b.get(d + 3);
                        if (0 == c.buff.b.length) {
                            var p = fc.ly = fc.ly;
                            c.buff.b += H.string(" M " + (H.string(Math.round(1E3 * (fc.lx = fc.lx)) / 1E3) + "," + H.string(Math.round(1E3 * p) / 1E3)))
                        }
                        k = "Q " + (H.string(Math.round(1E3 * k) / 1E3) + "," + H.string(Math.round(1E3 * n) / 1E3)) + " ";
                        h = fc.ly = h;
                        c.buff.b += H.string(" " + (k + (H.string(Math.round(1E3 * (fc.lx = f)) / 1E3) + "," + H.string(Math.round(1E3 * h) / 1E3))));
                        d += 4;
                        break;
                    case 4:
                        a = b.get(d + 2);
                        b = fc.ly = b.get(d + 3);
                        c.buff.b += H.string(" M " + (H.string(Math.round(1E3 * (fc.lx =
                            a)) / 1E3) + "," + H.string(Math.round(1E3 * b) / 1E3)));
                        break a;
                    case 5:
                        a = b.get(d + 2);
                        b = b.get(d + 3);
                        0 == c.buff.b.length && (d = fc.ly = fc.ly, c.buff.b += H.string(" M " + (H.string(Math.round(1E3 * (fc.lx = fc.lx)) / 1E3) + "," + H.string(Math.round(1E3 * d) / 1E3))));
                        b = fc.ly = b;
                        c.buff.b += H.string(" L " + (H.string(Math.round(1E3 * (fc.lx = a)) / 1E3) + "," + H.string(Math.round(1E3 * b) / 1E3)));
                        break a;
                    case 6:
                        f = b.get(d);
                        h = b.get(d + 1);
                        k = b.get(d + 2);
                        n = b.get(d + 3);
                        p = b.get(d + 4);
                        var P = b.get(d + 5);
                        c.buff.b += H.string(" C " + (f + "," + h + " " + k + "," + n + " " + p + "," + P));
                        d += 6
                }
                return c.xml()
            };
            Ja.drawChildren = function(a, b) {
                for (var c = 0, d = a.get_numChildren(); c < d;) {
                    var f = c++;
                    f = a.getChildAt(f);
                    if (f.get_visible()) {
                        var h = null;
                        null != Ja.handleObject && (h = Ja.handleObject(f));
                        null == h && (f instanceof ka ? h = Ja.drawSprite(f) : f instanceof md ? h = Ja.drawShape(f) : f instanceof sc && (h = Ja.drawText(f)));
                        if (null != h) {
                            for (var k = 0, n = f.get_filters(); k < n.length;) {
                                var p = n[k];
                                ++k;
                                Ja.handleFilter(f, p, h, b)
                            }
                            b.addChild(h)
                        }
                    }
                }
            };
            Ja.drawText = function(a) {
                var b = a.get_defaultTextFormat(),
                    c = 1 < a.get_numLines(),
                    d = Oa.text(c ? "" : a.get_text(), null, "text-before-edge");
                Oa.style(d, Ja.svgFont(b));
                Oa.setFill(d, b.color);
                1 == a.get_scaleX() && 1 == a.get_scaleY() || Oa.scale(d, a.get_scaleX(), a.get_scaleY());
                0 != a.get_rotation() && Oa.rotate(d, a.get_rotation());
                1 > a.get_alpha() && Oa.setOpacity(d, a.get_alpha());
                if (c)
                    for (Oa.translate(d, a.get_x(), a.get_y()), b = 0, c = a.get_numLines(); b < c;) {
                        var f = b++,
                            h = Oa.tspan(a.getLineText(f));
                        f = a.getCharBoundaries(a.getLineOffset(f));
                        Oa.x(h, f.x * a.get_scaleX());
                        Oa.y(h, f.y * a.get_scaleY());
                        d.addChild(h)
                    } else f =
                        a.getCharBoundaries(a.getLineOffset(0)), Oa.translate(d, a.get_x() + f.x * a.get_scaleX(), a.get_y() + f.y * a.get_scaleY());
                return d
            };
            Ja.substituteGenerics = function(a) {
                switch (a) {
                    case "_sans":
                        return "sans-serif";
                    case "_serif":
                        return "serif";
                    case "_typewriter":
                        return "monospace";
                    default:
                        return a
                }
            };
            Ja.svgFont = function(a) {
                var b = Ja.substituteFont(a.font),
                    c = "font: ";
                a.bold && (c += "bold ");
                a.italic && (c += "italic ");
                c += "" + a.size + "px " + b;
                0 != a.letterSpacing && (c += "; letter-spacing: " + a.letterSpacing + "px");
                return c
            };
            Ja.resetGradients =
                function() {
                    Ja.gradients = []
                };
            Ja.getGradients = function() {
                for (var a = W.createElement("defs"), b = 0, c = Ja.gradients.length; b < c;) {
                    var d = b++,
                        f = "grad" + (d + 1);
                    d = Ja.gradients[d];
                    if (0 == d.type) {
                        f = Oa.linearGradient(f, null, d.matrix);
                        for (var h = 0, k = d.colors.length; h < k;) {
                            var n = h++;
                            f.addChild(Oa.stop(d.ratios[n] / 255, d.colors[n], d.alphas[n]))
                        }
                        a.addChild(f)
                    } else {
                        f = Oa.radialGradient(f, null, d.matrix);
                        h = 0;
                        for (k = d.colors.length; h < k;) n = h++, f.addChild(Oa.stop(d.ratios[n] / 255, d.colors[n], d.alphas[n]));
                        a.addChild(f)
                    }
                }
                return a
            };
            Ja.resetImports = function() {
                Ja.imports = []
            };
            Ja.addImport = function(a) {
                -1 == Ja.imports.indexOf(a) && Ja.imports.push(a)
            };
            Ja.getImports = function() {
                for (var a = W.createElement("style"), b = "", c = 0, d = Ja.imports; c < d.length;) {
                    var f = d[c];
                    ++c;
                    b += H.string('@import url("' + f + '");')
                }
                a.addChild(W.createCData(b));
                return a
            };
            Ja.handleFilter = function(a, b, c, d) {
                if (b instanceof Sc) {
                    for (var f = 1; null != a.parent;) f *= a.get_scaleX(), a = a.parent;
                    f = a.get_scaleX();
                    a = W.parse(Df.print(c)).firstElement();
                    Oa.setStroke(a, b.get_color(), 2 * b.get_blurX() /
                        f);
                    1 > b.get_alpha() && Oa.strokeOpacity(a, b.get_alpha());
                    d.addChild(a)
                }
            };
            var Hf = function() {};
