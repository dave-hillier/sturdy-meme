/*
 * lime-init/06-rendering.js
 * Part 6/8: Rendering Pipeline
 * Contains: OpenFL rendering, graphics context, BitmapData operations
 */
            } if (h == f) break;
        else k = [a[h]], n = b[h]
    }
    return c
};
uc.simplify = function(a, b, c) {
    null == c && (c = 1.01);
    null == b && (b = 0);
    var d = Sa.area(a);
    0 == b && (b = d);
    for (var f = d, h = a.length; 3 <= h;) {
        for (var k = !1, n = 0; n < h;) {
            var p = qa.triArea(a[(n + h - 1) % h], a[n], a[(n + 1) % h]),
                g = (f - p) / d;
            Math.abs(p) < b && g < c && 1 / g < c ? (a.splice(n, 1), k = !0, f -= p, --h) : ++n
        }
        if (!k) break
    }
};
uc.visvalingam =
    function(a, b, c) {
        null == c && (c = 1.1);
        null == b && (b = 2);
        for (var d = a.length, f = [], h = 0, k = d; h < k;) {
            var n = h++;
            f.push(n < d - 1 ? I.distance(a[n], a[n + 1]) : 0)
        }
        var p = f;
        f = [];
        h = 0;
        for (k = d; h < k;) n = h++, f.push(0 == n || n == d - 1 ? 0 : Math.abs(qa.triArea(a[n - 1], a[n], a[n + 1])));
        k = f;
        var g = 0;
        for (f = 0; f < p.length;) h = p[f], ++f, g += h;
        for (var q = g; d > b;) {
            n = 1;
            var m = k[n];
            f = 1;
            for (h = d - 1; f < h;) {
                var u = f++;
                m > k[u] && (m = k[u], n = u)
            }
            h = I.distance(a[n - 1], a[n + 1]);
            q = q - p[n - 1] - p[n] + h;
            if (g / q > c) break;
            --d;
            a.splice(n, 1);
            p.splice(n, 1);
            p[n - 1] = h;
            k.splice(n, 1);
            1 < n && (k[n - 1] = Math.abs(qa.triArea(a[n -
                2], a[n - 1], a[n])));
            n < d - 1 && (k[n] = Math.abs(qa.triArea(a[n - 1], a[n], a[n + 1])))
        }
    };
uc.resampleClosed = function(a, b) {
    var c = a.length,
        d = Sa.perimeter(a);
    b = d / Math.round(d / b);
    d = a[c - 1];
    for (var f = [d], h = b, k = 0, n = 0; n < c;) {
        var p = n++,
            g = d;
        d = a[p];
        for (p = I.distance(g, d); k + p > h;) f.push(qa.lerp(g, d, (h - k) / p)), h += b;
        k += p
    }
    return f
};
uc.fractalizeClosed = function(a, b, c) {
    null == c && (c = .5);
    null == b && (b = 1);
    var d = [],
        f = null;
    f = function(a, b, h) {
        if (0 < h) {
            var k = new I(a.y - b.y, b.x - a.x),
                n = qa.lerp(a, b),
                p = c * (((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 +
                    (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 + (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647) / 3 * 2 - 1);
            n.x += k.x * p;
            n.y += k.y * p;
            f(a, n, h - 1);
            d.push(n);
            f(n, b, h - 1)
        }
    };
    for (var h = a.length, k = 0; k < h;) {
        var n = k++,
            p = a[n];
        n = a[(n + 1) % h];
        d.push(p);
        f(p, n, b)
    }
    return d
};
var de = function(a) {
    this.map = new Qa;
    null == de.phonemes && (de.phonemes = nd.VOWELS.concat(nd.CONSONANTS), de.phonemes.sort(function(a, b) {
        return b.length - a.length
    }));
    this.source = a;
    for (var b = 0; b < a.length;) {
        var c = a[b];
        ++b;
        if ("" != c) {
            c = de.split(c.toLowerCase());
            for (var d = [], f = 0; f < c.length;) {
                var h = c[f];
                ++f;
                var k = d.join("");
                Object.prototype.hasOwnProperty.call(this.map.h, k) ? this.map.h[k].push(h) : this.map.h[k] = [h];
                d.push(h);
                2 < d.length && d.shift()
            }
            c = d.join("");
            Object.prototype.hasOwnProperty.call(this.map.h, c) ? this.map.h[c].push("") : this.map.h[c] = [""]
        }
    }
};
g["com.watabou.nlp.Markov"] = de;
de.__name__ = "com.watabou.nlp.Markov";
de.split = function(a) {
    for (var b = [];
        "" != a;) {
        for (var c = !1, d = 0, f = de.phonemes; d < f.length;) {
            var h = f[d];
            ++d;
            if (N.substr(a, -h.length, null) == h) {
                b.unshift(h);
                a =
                    N.substr(a, 0, a.length - h.length);
                c = !0;
                break
            }
        }
        c || (a = N.substr(a, 0, a.length - 1))
    }
    return b
};
de.prototype = {
    generate: function(a) {
        for (null == a && (a = -1);;) {
            for (var b = "", c = [], d = Z.random(this.map.h[""]);
                "" != d;) {
                b += d;
                c.push(d);
                2 < c.length && c.shift();
                d = this.map;
                var f = c.join("");
                d = Z.random(d.h[f])
            }
            if (-1 == a || nd.splitWord(b).length <= a) return b
        }
    },
    __class__: de
};
var nd = function() {};
g["com.watabou.nlp.Syllables"] = nd;
nd.__name__ = "com.watabou.nlp.Syllables";
nd.split = function(a) {
    var b = [],
        c = 0;
    for (a = a.split(" "); c < a.length;) {
        var d =
            a[c];
        ++c;
        "" != d && (b = b.concat(nd.splitWord(d)))
    }
    return b
};
nd.splitWord = function(a) {
    for (var b = []; 0 < a.length;) {
        var c = 0 == b.length && "e" == N.substr(a, -1, null) ? nd.pinch(N.substr(a, 0, a.length - 1)) + "e" : nd.pinch(a);
        b.unshift(c);
        a = N.substr(a, 0, a.length - c.length);
        Z.every(nd.VOWELS, function(b) {
            return -1 == a.indexOf(b)
        }) && (b[0] = a + b[0], a = "")
    }
    return b
};
nd.pinch = function(a) {
    for (var b = a.length - 1; 0 <= b && -1 == nd.VOWELS.indexOf(a.charAt(b));) --b;
    if (0 > b) return a;
    for (var c = 0, d = nd.VOWELS; c < d.length;) {
        var f = d[c];
        ++c;
        if (N.substr(a,
                b - (f.length - 1), f.length) == f) {
            b -= f.length;
            break
        }
    }
    if (0 > b) return a;
    c = 0;
    for (d = nd.CONSONANTS; c < d.length;)
        if (f = d[c], ++c, N.substr(a, b - (f.length - 1), f.length) == f) return N.substr(a, b - (f.length - 1), null);
    return N.substr(a, b + 1, null)
};
var Ei = function() {
    this.complete = new Nc
};
g["com.watabou.processes.Process"] = Ei;
Ei.__name__ = "com.watabou.processes.Process";
Ei.prototype = {
    onComplete: function(a) {
        null != a && this.complete.add(a);
        return this
    },
    __class__: Ei
};
var Ke = function() {
    this.complete = new Nc
};
g["com.watabou.processes.Tweener"] =
    Ke;
Ke.__name__ = "com.watabou.processes.Tweener";
Ke.create = function(a, b) {
    var c = new Ke;
    c.time = a;
    c.updateCallback = b;
    return c
};
Ke.run = function(a, b) {
    a = Ke.create(a, b);
    a.start();
    return a
};
Ke.__super__ = Ei;
Ke.prototype = v(Ei.prototype, {
    start: function() {
        this.passed = 0;
        this.paused = !1;
        this.updateCallback(0);
        return this.resume()
    },
    stop: function() {
        return this.pause()
    },
    resume: function() {
        rb.get_tick().add(l(this, this.update));
        this.paused = !1;
        return this
    },
    pause: function() {
        rb.get_tick().remove(l(this, this.update));
        this.paused = !0;
        return this
    },
    update: function(a) {
        this.paused || ((this.passed += a) < this.time ? this.updateCallback(this.passed / this.time) : (this.updateCallback(1), this.stop(), this.complete.dispatch()))
    },
    __class__: Ke
});
var ge = function() {};
g["com.watabou.system.Exporter"] = ge;
ge.__name__ = "com.watabou.system.Exporter";
ge.saveBinary = function(a, b, c) {
    b = ge.fixName(b);
    a = Td.toArrayBuffer(a);
    window.saveAs(new Blob([a], {
        type: c
    }), b, !0)
};
ge.saveText = function(a, b, c) {
    b = ge.fixName(b);
    window.saveAs(new Blob([a], {
        type: c
    }), b, !0)
};
ge.savePNG =
    function(a, b) {
        a = a.encode(a.rect, new ri);
        ge.saveBinary(a, b + ".png", "image/png")
    };
ge.fixName = function(a) {
    return (new ja("[ ']", "g")).split(a.toLowerCase()).join("_")
};
var ba = function() {};
g["com.watabou.system.State"] = ba;
ba.__name__ = "com.watabou.system.State";
ba.init = function(a, b) {
    null == ba.so && (null == a && (a = A.current.meta.h.packageName), ba.so = $c.getLocal(a), ba.data = ba.so.data, null != b && 0 == ya.fields(ba.data).length && b(ba.data))
};
ba.get = function(a, b) {
    ba.init();
    return Object.prototype.hasOwnProperty.call(ba.data,
        a) ? ba.data[a] : b
};
ba.set = function(a, b) {
    ba.init();
    ba.data[a] = b;
    ba.so.flush()
};
var za = function() {};
g["com.watabou.system.URLState"] = za;
za.__name__ = "com.watabou.system.URLState";
za.init = function() {
    if (null == za.data) {
        za.data = {};
        var a = new URLSearchParams(E.location.search);
        null != a && a.forEach(function(a, c) {
            return za.data[c] = a
        })
    }
};
za.reset = function() {
    za.data = {};
    za.update()
};
za.get = function(a, b) {
    za.init();
    return Object.prototype.hasOwnProperty.call(za.data, a) ? za.data[a] : b
};
za.getInt = function(a, b) {
    null == b &&
        (b = 0);
    za.init();
    return Object.prototype.hasOwnProperty.call(za.data, a) ? H.parseInt(za.data[a]) : b
};
za.getFlag = function(a, b) {
    null == b && (b = !1);
    return 0 != za.getInt(a, b ? 1 : 0)
};
za.set = function(a, b) {
    za.init();
    za.data[a] = b;
    za.update()
};
za.setFlag = function(a, b) {
    null == b && (b = !0);
    za.set(a, b ? "1" : "0")
};
za.getParams = function() {
    for (var a = "", b = za.data, c = ya.fields(b), d = 0; d < c.length;) {
        var f = c[d++];
        a += ("" == a ? "?" : "&") + ("" + f + "=" + H.string(b[f]))
    }
    return a
};
za.getURL = function() {
    return za.baseURL + za.getParams()
};
za.update = function() {
    window.history.replaceState(za.data,
        "", za.getParams())
};
za.fromString = function(a) {
    za.data = {};
    a = N.substr(a, a.indexOf("?") + 1, null).split("&");
    for (var b = 0; b < a.length;) {
        var c = a[b];
        ++b;
        var d = c.indexOf("="),
            f = N.substr(c, 0, d);
        c = N.substr(c, d + 1, null);
        c = decodeURIComponent(c.split("+").join(" "));
        za.data[f] = c
    }
    za.update()
};
var wg = function(a) {
    this.ruleSet = a;
    this.clearState()
};
g["com.watabou.tracery.RuleSelector"] = wg;
wg.__name__ = "com.watabou.tracery.RuleSelector";
wg.prototype = {
    select: function() {
        var a = this.ruleSet.defaultRules,
            b = Oe.rng() * a.length |
            0;
        return a[b]
    },
    clearState: function() {},
    __class__: wg
};
var $h = function(a) {
    wg.call(this, a)
};
g["com.watabou.tracery.DeckRuleSelector"] = $h;
$h.__name__ = "com.watabou.tracery.DeckRuleSelector";
$h.__super__ = wg;
$h.prototype = v(wg.prototype, {
    select: function() {
        0 == this.deck.length && this.clearState();
        var a = Oe.rng() * this.deck.length | 0,
            b = this.deck[a];
        this.deck.splice(a, 1);
        return b
    },
    clearState: function() {
        this.deck = this.ruleSet.defaultRules.slice()
    },
    __class__: $h
});
var Rf = function(a, b, c) {
    this.grammar = a;
    this.key = b;
    this.baseRules =
        c;
    this.clearState()
};
g["com.watabou.tracery.Symbol"] = Rf;
Rf.__name__ = "com.watabou.tracery.Symbol";
Rf.prototype = {
    clearState: function() {
        this.stack = [this.baseRules];
        this.baseRules.clearState()
    },
    pushRules: function(a) {
        this.pushRuleSet(new xg(this.grammar, a))
    },
    pushRuleSet: function(a) {
        this.stack.push(a)
    },
    popRules: function() {
        this.stack.pop()
    },
    selectRule: function() {
        return 0 == this.stack.length ? (hb.trace("The rule stack for " + this.key + " is empty, too many pops?", {
            fileName: "com/watabou/tracery/Symbol.hx",
            lineNumber: 37,
            className: "com.watabou.tracery.Symbol",
            methodName: "selectRule"
        }), "((" + this.key + "))") : this.top().selectRule()
    },
    top: function() {
        return this.stack[this.stack.length - 1]
    },
    __class__: Rf
};
var Fi = function(a, b, c) {
    Rf.call(this, a, b, new xg(a, []));
    this.generator = c
};
g["com.watabou.tracery.ExtSymbol"] = Fi;
Fi.__name__ = "com.watabou.tracery.ExtSymbol";
Fi.__super__ = Rf;
Fi.prototype = v(Rf.prototype, {
    selectRule: function() {
        return this.generator()
    },
    __class__: Fi
});
var hk = function(a) {
    this.autoID = 0;
    this.defaultSelector = wg;
    this.modifiers =
        new Qa;
    this.flags = [];
    this.loadFromRawObj(a)
};
g["com.watabou.tracery.Grammar"] = hk;
hk.__name__ = "com.watabou.tracery.Grammar";
hk.prototype = {
    clearState: function() {
        for (var a = this.symbols.h, b = Object.keys(a), c = b.length, d = 0; d < c;) a[b[d++]].clearState();
        this.flags = []
    },
    addModifiers: function(a) {
        for (var b = Object.keys(a.h), c = b.length, d = 0; d < c;) {
            var f = b[d++];
            this.modifiers.h[f] = a.h[f]
        }
    },
    loadFromRawObj: function(a) {
        this.raw = a;
        this.symbols = new Qa;
        this.subgrammars = [];
        if (null != a)
            for (var b = ya.fields(a), c = 0; c < b.length;) {
                var d =
                    b[c++],
                    f = d;
                d = a[d];
                var h = "string" == typeof d ? [d] : d;
                null == h && (hb.trace(a, {
                    fileName: "com/watabou/tracery/Grammar.hx",
                    lineNumber: 55,
                    className: "com.watabou.tracery.Grammar",
                    methodName: "loadFromRawObj"
                }), hb.trace(f, {
                    fileName: "com/watabou/tracery/Grammar.hx",
                    lineNumber: 56,
                    className: "com.watabou.tracery.Grammar",
                    methodName: "loadFromRawObj"
                }));
                d = this.symbols;
                h = new Rf(this, f, new xg(this, this.unwrap(a, h)));
                d.h[f] = h
            }
    },
    unwrap: function(a, b) {
        for (var c = [], d = 0; d < b.length;) {
            var f = b[d];
            ++d;
            if ("@" == N.substr(f, 0, 1)) {
                var h =
                    N.substr(f, 1, null);
                f = null;
                var k = h.indexOf("?-"); - 1 != k && (f = N.substr(h, 0, k), h = N.substr(h, k + 2, null));
                k = a[h];
                h = 0;
                for (k = this.unwrap(a, "string" == typeof k ? [k] : k); h < k.length;) {
                    var n = k[h];
                    ++h;
                    c.push(null == f ? n : "" + f + "?-" + n)
                }
            } else c.push(f)
        }
        return c
    },
    createRoot: function(a) {
        return new ph(this, null, 0, {
            type: -1,
            raw: a
        })
    },
    expand: function(a, b) {
        null == b && (b = !1);
        a = this.createRoot(a);
        a.expand();
        b || a.clearEscapeChars();
        return a
    },
    flatten: function(a, b) {
        null == b && (b = !1);
        return this.expand(a, b).finishedText
    },
    execute: function(a) {
        "set " ==
        N.substr(a, 0, 4) ? (a = N.substr(a, 4, null), Z.addAll(this.flags, (new ja(", +", "")).split(a))) : "clear " == N.substr(a, 0, 6) ? (a = N.substr(a, 6, null), Z.addAll(this.flags, (new ja(", +", "")).split(a))) : hb.trace('Unknown function "' + a + '" is called', {
            fileName: "com/watabou/tracery/Grammar.hx",
            lineNumber: 113,
            className: "com.watabou.tracery.Grammar",
            methodName: "execute"
        })
    },
    pushRules: function(a, b) {
        if (Object.prototype.hasOwnProperty.call(this.symbols.h, a)) this.symbols.h[a].pushRules(b);
        else {
            var c = this.symbols;
            b = new Rf(this,
                a, new xg(this, b));
            c.h[a] = b
        }
    },
    popRules: function(a) {
        Object.prototype.hasOwnProperty.call(this.symbols.h, a) ? this.symbols.h[a].popRules() : hb.trace("Can't pop: no symbol for key " + a, {
            fileName: "com/watabou/tracery/Grammar.hx",
            lineNumber: 142,
            className: "com.watabou.tracery.Grammar",
            methodName: "popRules"
        })
    },
    addAutoRules: function(a) {
        var b = "_auto" + this.autoID++;
        this.pushRules(b, a);
        return b
    },
    selectRule: function(a) {
        for (var b = this.symbols.h[a], c = 0, d = this.flags; c < d.length;) {
            var f = d[c];
            ++c;
            f = this.symbols.h["" + f +
                "?-" + a];
            if (null != f) {
                b = f;
                break
            }
        }
        if (null != b && (c = b.selectRule(), null != c)) return c;
        c = 0;
        for (d = this.subgrammars; c < d.length;)
            if (b = d[c], ++c, Object.prototype.hasOwnProperty.call(b.symbols.h, a)) return b.symbols.h[a].selectRule();
        hb.trace('No symbol for "' + a + '"', {
            fileName: "com/watabou/tracery/Grammar.hx",
            lineNumber: 177,
            className: "com.watabou.tracery.Grammar",
            methodName: "selectRule"
        });
        return "((" + a + "))"
    },
    validateRule: function(a) {
        var b = a.indexOf("?-");
        if (-1 == b) return a;
        var c = N.substr(a, 0, b);
        return this.eval(c) ?
            N.substr(a, b + 2, null) : null
    },
    eval: function(a) {
        var b = parseFloat(a);
        if (!isNaN(b)) return a = b, null == a && (a = .5), (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 < a;
        b = a.split("&");
        if (1 < b.length) {
            for (a = 0; a < b.length;) {
                var c = b[a];
                ++a;
                if (!this.eval(c)) return !1
            }
            return !0
        }
        a = (b = "!" == a.charAt(0)) ? N.substr(a, 1, null) : a;
        return -1 != this.flags.indexOf(a) != b
    },
    addExternal: function(a, b) {
        var c = this.symbols;
        b = new Fi(this, a, b);
        c.h[a] = b
    },
    __class__: hk
};
var jb = function() {};
g["com.watabou.tracery.ModsEngBasic"] = jb;
jb.__name__ = "com.watabou.tracery.ModsEngBasic";
jb.isVowel = function(a) {
    return -1 != "ieaou".indexOf(a.toLowerCase())
};
jb.isAlphaNum = function(a) {
    return "a" <= a && "z" >= a || "A" <= a && "Z" >= a ? !0 : "0" <= a ? "9" >= a : !1
};
jb.isPlural = function(a) {
    a = a.toLowerCase();
    return "s" == N.substr(a, -1, null) ? "ss" != N.substr(a, -2, null) : !1
};
jb.escapeRegExp = function(a) {
    return a.replace(/([.*+?^=!:${}()|\[\]/\\])/g, "\\$1")
};
jb.replace = function(a, b) {
    var c = new RegExp(jb.escapeRegExp(b[0]), "g");
    return a.replace(c, b[1])
};
jb.capitalizeAll = function(a, b) {
    b = "";
    for (var c = !0, d = 0, f = a.length; d < f;) {
        var h =
            d++;
        h = a.charAt(h);
        jb.isAlphaNum(h) || "'" == h ? c ? (b += h.toUpperCase(), c = !1) : b += h : (c = !0, b += h)
    }
    return b
};
jb.capitalize = function(a, b) {
    return a.charAt(0).toUpperCase() + N.substr(a, 1, null)
};
jb.caps = function(a, b) {
    return a.toUpperCase()
};
jb.a = function(a, b) {
    if (0 < a.length) {
        if ("u" == a.charAt(0).toLowerCase() && 2 < a.length && "i" == a.charAt(2).toLowerCase()) return "a " + a;
        if (jb.isVowel(a.charAt(0))) return "an " + a
    }
    return "a " + a
};
jb.firstS = function(a, b) {
    a = a.split(" ");
    return 1 == a.length ? jb.s(a[0], null) : jb.s(a[0], null) + " " + a.slice(1).join(" ")
};
jb.s = function(a, b) {
    b = jb.plurals.h;
    for (var c = Object.keys(b), d = c.length, f = 0; f < d;) {
        var h = c[f++],
            k = h;
        h = b[h];
        if (N.substr(a, -k.length, null) == k) return N.substr(a, 0, a.length - k.length) + h
    }
    b = N.substr(a, -1, null);
    if ("s" == b || "x" == b || "z" == b) return a + "es";
    if ("y" == b && -1 == "ieaou".indexOf(N.substr(a, -2, 1))) return N.substr(a, 0, -1) + "ies";
    b = N.substr(a, -2, null);
    return "ch" == b || "sh" == b ? a + "es" : a + "s"
};
jb.possessive = function(a, b) {
    return "s" == N.substr(a, -1, null) ? a + "'" : a + "'s"
};
jb.ed = function(a, b) {
    switch (N.substr(a, -1, null)) {
        case "e":
            return a +
                "d";
        case "h":
            return a + "ed";
        case "s":
            return a + "ed";
        case "x":
            return a + "ed";
        case "y":
            return jb.isVowel(a.charAt(a.length - 2)) ? a + "d" : a.substring(0, a.length - 1) + "ied";
        default:
            return a + "ed"
    }
};
jb.ing = function(a, b) {
    return "e" == N.substr(a, -1, null) ? a.substring(0, a.length - 1) + "ing" : a + "ing"
};
jb.thiss = function(a, b) {
    return jb.isPlural(a) ? "these" : "this"
};
jb.they = function(a, b) {
    return jb.isPlural(a) ? "they" : "it"
};
jb.them = function(a, b) {
    return jb.isPlural(a) ? "them" : "it"
};
jb.is = function(a, b) {
    return jb.isPlural(a) ? "are" : "is"
};
jb.was = function(a, b) {
    return jb.isPlural(a) ? "were" : "was"
};
jb.get = function() {
    var a = new Qa;
    a.h.replace = jb.replace;
    a.h.possessive = jb.possessive;
    a.h.capitalize = jb.capitalize;
    a.h.capitalizeAll = jb.capitalizeAll;
    a.h.caps = jb.caps;
    a.h.firstS = jb.firstS;
    a.h.s = jb.s;
    a.h.a = jb.a;
    a.h.ed = jb.ed;
    a.h.ing = jb.ing;
    a.h["this"] = jb.thiss;
    a.h.they = jb.they;
    a.h.them = jb.them;
    a.h.is = jb.is;
    a.h.was = jb.was;
    return a
};
var qh = function(a, b) {
    this.node = a;
    a = b.split(":");
    this.target = a[0];
    1 == a.length ? this.type = 2 : (this.rule = a[1], this.type = "POP" ==
        this.rule ? 1 : 0)
};
g["com.watabou.tracery.NodeAction"] = qh;
qh.__name__ = "com.watabou.tracery.NodeAction";
qh.prototype = {
    createUndo: function() {
        return 0 == this.type ? new qh(this.node, this.target + ":POP") : null
    },
    activate: function() {
        var a = this.node.grammar;
        switch (this.type) {
            case 0:
                for (var b = this.rule.split(","), c = [], d = 0; d < b.length;) {
                    var f = b[d];
                    ++d;
                    f = new ph(a, null, 0, {
                        type: -1,
                        raw: f
                    });
                    f.expand();
                    c.push(f.finishedText)
                }
                a.pushRules(this.target, c);
                break;
            case 1:
                a.popRules(this.target);
                break;
            case 2:
                a.execute(this.target)
        }
    },
    __class__: qh
};
var xg = function(a, b) {
    this.grammar = a;
    a = [];
    for (var c = 0; c < b.length;) {
        var d = b[c];
        ++c;
        a.push(this.process(d))
    }
    this.defaultRules = this.raw = a
};
g["com.watabou.tracery.RuleSet"] = xg;
xg.__name__ = "com.watabou.tracery.RuleSet";
xg.prototype = {
    process: function(a) {
        var b = a.indexOf("{");
        if (-1 == b) return a;
        for (var c = -1, d = 1, f = [], h = b + 1, k = b + 1, n = a.length; k < n;) {
            var p = k++,
                g = a.charAt(p);
            if ("|" == g && 1 == d) f.push(a.substring(h, p)), h = p + 1;
            else if ("{" == g) ++d;
            else if ("}" == g && 0 == --d) {
                f.push(a.substring(h, p));
                c = p;
                break
            }
        }
        return -1 !=
            c ? (b = N.substr(a, 0, b), a = N.substr(a, c + 1, null), f = this.grammar.addAutoRules(1 == f.length ? [f[0], ""] : f), "" + b + "#" + f + "#" + this.process(a)) : a
    },
    selectRule: function() {
        null == this.selector && (this.selector = w.createInstance(this.grammar.defaultSelector, [this]));
        for (;;) {
            var a = this.grammar.validateRule(this.selector.select());
            if (null != a) return a
        }
    },
    clearState: function() {
        null != this.selector && this.selector.clearState()
    },
    __class__: xg
};
var Oe = function() {};
g["com.watabou.tracery.Tracery"] = Oe;
Oe.__name__ = "com.watabou.tracery.Tracery";
Oe.parseTag = function(a) {
    for (var b = {
            symbol: null,
            preactions: [],
            postactions: [],
            modifiers: []
        }, c = Oe.parse(a), d = null, f = 0; f < c.length;) {
        var h = c[f];
        ++f;
        if (0 == h.type)
            if (null == d) d = h.raw;
            else throw X.thrown("multiple main sections in " + a);
        else b.preactions.push(h.raw)
    }
    null != d && (a = d.split("."), b.symbol = a[0], b.modifiers = a.slice(1));
    return b
};
Oe.parse = function(a) {
    var b = 0,
        c = !1,
        d = [],
        f = !1,
        h = 0,
        k = "",
        n = -1;
    if (null == a) return [];
    for (var p = function(b, c, f) {
            1 > c - b && (1 == f && hb.trace("" + b + ": empty tag", {
                fileName: "com/watabou/tracery/Tracery.hx",
                lineNumber: 63,
                className: "com.watabou.tracery.Tracery",
                methodName: "parse"
            }), 2 == f && hb.trace("" + b + ": empty action", {
                fileName: "com/watabou/tracery/Tracery.hx",
                lineNumber: 65,
                className: "com.watabou.tracery.Tracery",
                methodName: "parse"
            }));
            b = -1 != n ? k + "\\" + a.substring(n + 1, c) : a.substring(b, c);
            d.push({
                type: f,
                raw: b
            });
            n = -1;
            k = ""
        }, g = 0, q = a.length; g < q;) {
        var m = g++;
        if (f) f = !f;
        else switch (a.charAt(m)) {
            case "#":
                0 == b && (c ? p(h, m, 1) : h < m && p(h, m, 0), h = m + 1, c = !c);
                break;
            case "[":
                0 != b || c || (h < m && p(h, m, 0), h = m + 1);
                ++b;
                break;
            case "\\":
                f = !0;
                k += a.substring(h, m);
                h = m + 1;
                n = m;
                break;
            case "]":
                --b, 0 != b || c || (p(h, m, 2), h = m + 1)
        }
    }
    h < a.length && p(h, a.length, 0);
    c && hb.trace("Unclosed tag", {
        fileName: "com/watabou/tracery/Tracery.hx",
        lineNumber: 130,
        className: "com.watabou.tracery.Tracery",
        methodName: "parse"
    });
    0 < b && hb.trace("Too many [", {
        fileName: "com/watabou/tracery/Tracery.hx",
        lineNumber: 132,
        className: "com.watabou.tracery.Tracery",
        methodName: "parse"
    });
    0 > b && hb.trace("Too many ]", {
        fileName: "com/watabou/tracery/Tracery.hx",
        lineNumber: 134,
        className: "com.watabou.tracery.Tracery",
        methodName: "parse"
    });
    g = [];
    q = 0;
    for (b = d; q < b.length;) c = b[q], ++q, (0 != c.type || 0 < c.raw.length) && g.push(c);
    return d = g
};
var ph = function(a, b, c, d) {
    null == d.raw && (hb.trace("Empty input for node", {
        fileName: "com/watabou/tracery/TraceryNode.hx",
        lineNumber: 35,
        className: "com.watabou.tracery.TraceryNode",
        methodName: "new"
    }), d.raw = "");
    this.grammar = a;
    this.parent = b;
    this.depth = null != b ? b.depth + 1 : 0;
    this.childIndex = c;
    this.raw = d.raw;
    this.type = d.type;
    this.isExpanded = !1
};
g["com.watabou.tracery.TraceryNode"] = ph;
ph.__name__ = "com.watabou.tracery.TraceryNode";
ph.prototype = {
    expandChildren: function(a, b) {
        this.children = [];
        this.finishedText = "";
        this.childRule = a;
        if (null != a) {
            a = Oe.parse(a);
            for (var c = 0, d = a.length; c < d;) {
                var f = c++,
                    h = new ph(this.grammar, this, f, a[f]);
                this.children[f] = h;
                b || h.expand(!1);
                this.finishedText += h.finishedText
            }
        } else hb.trace("No child rule provided, can't expand children", {
            fileName: "com/watabou/tracery/TraceryNode.hx",
            lineNumber: 72,
            className: "com.watabou.tracery.TraceryNode",
            methodName: "expandChildren"
        })
    },
    expand: function(a) {
        null == a && (a = !1);
        if (!this.isExpanded) switch (this.isExpanded = !0, this.type) {
            case 0:
                this.finishedText = this.raw;
                break;
            case 1:
                var b = Oe.parseTag(this.raw);
                this.symbol = b.symbol;
                this.modifiers = b.modifiers;
                var c = [],
                    d = 0;
                for (b = b.preactions; d < b.length;) {
                    var f = b[d];
                    ++d;
                    c.push(new qh(this, f))
                }
                this.preactions = c;
                this.postaction = [];
                c = 0;
                for (d = this.preactions; c < d.length;) f = d[c], ++c, 0 == f.type && this.postaction.push(f.createUndo());
                c = 0;
                for (d = this.preactions; c < d.length;) f = d[c], ++c, f.activate();
                this.finishedText = this.raw;
                c = this.grammar.selectRule(this.symbol);
                this.expandChildren(c, a);
                c = 0;
                for (d = this.modifiers; c < d.length;) a = d[c], ++c, b = [], f = a.indexOf("("), -1 != f && (b = a.substring(f + 1, a.indexOf(")")).split(","), a = a.substring(0, f)), f = this.grammar.modifiers.h[a], null == f ? (hb.trace("Missing modifier " + a, {
                    fileName: "com/watabou/tracery/TraceryNode.hx",
                    lineNumber: 114,
                    className: "com.watabou.tracery.TraceryNode",
                    methodName: "expand"
                }), this.finishedText += "((." + a + "))") : this.finishedText = f(this.finishedText, b);
                c = 0;
                for (d = this.postaction; c < d.length;) a = d[c], ++c, a.activate();
                break;
            case 2:
                this.action = new qh(this, this.raw);
                this.action.activate();
                this.finishedText = "";
                break;
            default:
                this.expandChildren(this.raw, a)
        }
    },
    clearEscapeChars: function() {},
    __class__: ph
};
var Z = function() {};
g["com.watabou.utils.ArrayExtender"] = Z;
Z.__name__ = "com.watabou.utils.ArrayExtender";
Z.revert = function(a) {
    a = a.slice();
    a.reverse();
    return a
};
Z.shuffle = function(a) {
    for (var b = [], c = 0; c < a.length;) {
        var d = a[c];
        ++c;
        b.splice((C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * (b.length + 1) | 0, 0, d)
    }
    return b
};
Z.random =
    function(a) {
        return a[(C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a.length | 0]
    };
Z.pick = function(a) {
    var b = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * a.length | 0,
        c = a[b];
    a.splice(b, 1);
    return c
};
Z.subset = function(a, b) {
    return Z.shuffle(a).slice(0, b)
};
Z.weighted = function(a, b) {
    for (var c = (C.seed = 48271 * C.seed % 2147483647 | 0) / 2147483647 * Z.sum(b), d = 0, f = 0, h = a.length; f < h;) {
        var k = f++;
        if (c <= (d += b[k])) return a[k]
    }
    return a[0]
};
Z.isEmpty = function(a) {
    return 0 == a.length
};
Z.min = function(a, b) {
    for (var c = a[0], d = b(c), f = 1,
            h = a.length; f < h;) {
        var k = f++;
        k = a[k];
        var n = b(k);
        n < d && (c = k, d = n)
    }
    return c
};
Z.max = function(a, b) {
    for (var c = a[0], d = b(c), f = 1, h = a.length; f < h;) {
        var k = f++;
        k = a[k];
        var n = b(k);
        n > d && (c = k, d = n)
    }
    return c
};
Z.every = function(a, b) {
    for (var c = 0; c < a.length;) {
        var d = a[c];
        ++c;
        if (!b(d)) return !1
    }
    return !0
};
Z.some = function(a, b) {
    for (var c = 0; c < a.length;) {
        var d = a[c];
        ++c;
        if (b(d)) return !0
    }
    return !1
};
Z.count = function(a, b) {
    for (var c = 0, d = 0; d < a.length;) {
        var f = a[d];
        ++d;
        b(f) && ++c
    }
    return c
};
Z.find = function(a, b) {
    for (var c = 0; c < a.length;) {
        var d =
            a[c];
        ++c;
        if (b(d)) return d
    }
    return null
};
Z.sum = function(a) {
    for (var b = 0, c = 0; c < a.length;) {
        var d = a[c];
        ++c;
        b += d
    }
    return b
};
Z.replace = function(a, b, c) {
    b = a.indexOf(b);
    a[b++] = c[0];
    for (var d = 1, f = c.length; d < f;) {
        var h = d++;
        a.splice(b++, 0, c[h])
    }
};
Z.add = function(a, b) {
    return -1 == a.indexOf(b) ? (a.push(b), !0) : !1
};
Z.intersect = function(a, b) {
    for (var c = [], d = 0; d < a.length;) {
        var f = a[d];
        ++d; - 1 != b.indexOf(f) && c.push(f)
    }
    return c
};
Z.addAll = function(a, b) {
    for (var c = 0; c < b.length;) {
        var d = b[c];
        ++c; - 1 == a.indexOf(d) && a.push(d)
    }
};
Z.collect =
    function(a) {
        for (var b = [], c = 0; c < a.length;) {
            var d = a[c];
            ++c;
            Z.addAll(b, d)
        }
        return b
    };
Z.removeAll = function(a, b) {
    for (var c = 0; c < b.length;) {
        var d = b[c];
        ++c;
        N.remove(a, d)
    }
};
Z.difference = function(a, b) {
    for (var c = [], d = 0; d < a.length;) {
        var f = a[d];
        ++d; - 1 == b.indexOf(f) && c.push(f)
    }
    return c
};
Z.intersects = function(a, b) {
    for (var c = 0; c < a.length;) {
        var d = a[c];
        ++c;
        if (-1 != b.indexOf(d)) return !0
    }
    return !1
};
Z.sortBy = function(a, b) {
    for (var c = [], d = 0, f = a.length; d < f;) {
        var h = d++;
        c.push(h)
    }
    var k = c;
    c = [];
    for (d = 0; d < a.length;) f = a[d], ++d, c.push(b(f));
    var n = c;
    k.sort(function(a, b) {
        a = n[a] - n[b];
        return 0 == a ? 0 : 0 > a ? -1 : 1
    });
    c = [];
    d = 0;
    for (f = a.length; d < f;) h = d++, c.push(a[k[h]]);
    return c
};
var Sh = function() {};
g["com.watabou.utils.ColorNames"] = Sh;
Sh.__name__ = "com.watabou.utils.ColorNames";
Sh.get = function(a) {
    for (var b = a >>> 16, c = a >>> 8 & 255, d = a & 255, f = "", h = 1E10, k = Sh.values.h, n = Object.keys(k), p = n.length, g = 0; g < p;) {
        var q = n[g++],
            m = q;
        a = k[q];
        q = a >> 16;
        var u = a >> 8 & 255;
        a &= 255;
        q = (b - q) * (b - q) + (c - u) * (c - u) + (d - a) * (d - a);
        h > cb.toFloat(q) && (h = cb.toFloat(q), f = m)
    }
    return f
};
var cl = function() {};
g["com.watabou.utils.DisplayObjectExtender"] = cl;
cl.__name__ = "com.watabou.utils.DisplayObjectExtender";
cl.onActivate = function(a, b) {
    var c = function(a) {
        b("addedToStage" == a.type)
    };
    a.addEventListener("addedToStage", c);
    a.addEventListener("removedFromStage", c)
};
var Kb = function() {};
g["com.watabou.utils.GraphicsExtender"] = Kb;
Kb.__name__ = "com.watabou.utils.GraphicsExtender";
Kb.drawPolygon = function(a, b) {
    var c = b[b.length - 1];
    a.moveTo(c.x, c.y);
    for (var d = 0; d < b.length;) c = b[d], ++d, a.lineTo(c.x, c.y)
};
Kb.drawPolygonAt =
    function(a, b, c, d) {
        var f = b[b.length - 1];
        a.moveTo(f.x + c, f.y + d);
        for (var h = 0; h < b.length;) f = b[h], ++h, a.lineTo(f.x + c, f.y + d)
    };
Kb.drawPolyline = function(a, b) {
    var c = b[0];
    a.moveTo(c.x, c.y);
    for (var d = 1, f = b.length; d < f;) c = d++, c = b[c], a.lineTo(c.x, c.y)
};
Kb.dashedPolyline = function(a, b, c, d) {
    null == c && (c = !1);
    if (!(2 > b.length)) {
        var f = !0,
            h = 0,
            k = 0,
            n = d[0],
            p = c ? -1 : 0,
            g = b[c ? b.length - 1 : 0];
        c = b[c ? 0 : 1];
        for (a.moveTo(g.x, g.y);;) {
            var q = I.distance(g, c);
            if (k + q < n) {
                f && a.lineTo(c.x, c.y);
                if (++p >= b.length) break;
                g = c;
                c = b[p];
                k += q
            } else 0 < n && (g =
                qa.lerp(g, c, (n - k) / q), f ? a.lineTo(g.x, g.y) : a.moveTo(g.x, g.y)), ++h >= d.length && (h = 0), n = d[h], k = 0, f = !f
        }
    }
};
var Fc = function() {};
g["com.watabou.utils.MathUtils"] = Fc;
Fc.__name__ = "com.watabou.utils.MathUtils";
Fc.gate = function(a, b, c) {
    return a < b ? b : a < c ? a : c
};
Fc.cycle = function(a, b, c) {
    for (; a < b;) a += c - b;
    for (; a > c;) a -= c - b;
    return a
};
Fc.gatei = function(a, b, c) {
    return a < b ? b : a < c ? a : c
};
var jc = y["com.watabou.utils.ParamType"] = {
    __ename__: "com.watabou.utils.ParamType",
    __constructs__: null,
    COLOR: {
        _hx_name: "COLOR",
        _hx_index: 0,
        __enum__: "com.watabou.utils.ParamType",
        toString: r
    },
    MULTI: {
        _hx_name: "MULTI",
        _hx_index: 1,
        __enum__: "com.watabou.utils.ParamType",
        toString: r
    },
    FONT: {
        _hx_name: "FONT",
        _hx_index: 2,
        __enum__: "com.watabou.utils.ParamType",
        toString: r
    },
    FLOAT: {
        _hx_name: "FLOAT",
        _hx_index: 3,
        __enum__: "com.watabou.utils.ParamType",
        toString: r
    },
    INT: {
        _hx_name: "INT",
        _hx_index: 4,
        __enum__: "com.watabou.utils.ParamType",
        toString: r
    },
    STRING: {
        _hx_name: "STRING",
        _hx_index: 5,
        __enum__: "com.watabou.utils.ParamType",
        toString: r
    },
    BOOL: {
        _hx_name: "BOOL",
        _hx_index: 6,
        __enum__: "com.watabou.utils.ParamType",
        toString: r
    }
};
jc.__constructs__ = [jc.COLOR, jc.MULTI, jc.FONT, jc.FLOAT, jc.INT, jc.STRING, jc.BOOL];
var Xc = function() {
    this.params = []
};
g["com.watabou.utils.Palette"] = Xc;
Xc.__name__ = "com.watabou.utils.Palette";
Xc.float2str = function(a) {
    a = null == a ? "null" : "" + a; - 1 == a.indexOf(".") && (a += ".0");
    return a
};
Xc.font2format = function(a) {
    if (null == a) return null;
    var b = null != a.face ? a.face : null != a.embedded && ac.exists(a.embedded) ? ac.getFont(a.embedded).name : "_serif";
    return new we(b, a.size, 0, a.bold, a.italic)
};
Xc.fromData = function(a) {
    for (var b =
            new Xc, c = ya.fields(a), d = 0; d < c.length;) {
        var f = c[d++],
            h = f;
        f = a[f];
        if ("number" == typeof f && (f | 0) === f) b.setInt(h, f);
        else if ("number" == typeof f) b.setFloat(h, f);
        else if ("boolean" == typeof f) b.setBool(h, f);
        else if ("string" == typeof f) {
            if (7 == f.length && "#" == f.charAt(0)) {
                var k = H.parseInt("0x" + N.substr(f, 1, null));
                if (null != k) {
                    b.setColor(h, k);
                    continue
                }
            }
            k = parseFloat(f);
            if (isNaN(k)) switch (f) {
                case "false":
                    b.setBool(h, !1);
                    break;
                case "true":
                    b.setBool(h, !0);
                    break;
                default:
                    b.setString(h, f)
            } else - 1 == f.indexOf(".") ? b.setInt(h,
                k | 0) : b.setFloat(h, k)
        } else if (f instanceof Array) {
            k = [];
            for (var n = 0; n < f.length;) {
                var p = f[n];
                ++n;
                p = H.parseInt("0x" + N.substr(p, 1, null));
                null != p && k.push(p)
            }
            b.setMulti(h, k)
        } else null != f && b.setFont(h, f)
    }
    return b
};
Xc.fromJSON = function(a) {
    a = JSON.parse(a);
    return Xc.fromData(a)
};
Xc.fromAsset = function(a) {
    return ac.exists(a) ? Xc.fromJSON(ac.getText(a)) : null
};
Xc.prototype = {
    getColor: function(a, b) {
        null == b && (b = 0);
        for (var c = 0, d = this.params; c < d.length;) {
            var f = d[c];
            ++c;
            if (f.id == a) {
                if (f.type == jc.COLOR) return f.color;
                if (f.type ==
                    jc.MULTI) return f.multi[0];
                break
            }
        }
        return b
    },
    getMulti: function(a, b) {
        null == b && (b = 0);
        for (var c = 0, d = this.params; c < d.length;) {
            var f = d[c];
            ++c;
            if (f.id == a) {
                if (f.type == jc.MULTI) return f.multi;
                if (f.type == jc.COLOR) return [f.color];
                break
            }
        }
        return [b]
    },
    getFont: function(a, b) {
        for (var c = 0, d = this.params; c < d.length;) {
            var f = d[c];
            ++c;
            if (f.id == a && f.type == jc.FONT) return f.font
        }
        return b
    },
    getFloat: function(a, b) {
        null == b && (b = 0);
        for (var c = 0, d = this.params; c < d.length;) {
            var f = d[c];
            ++c;
            if (f.id == a && f.type == jc.FLOAT) return f.float
        }
        return b
    },
    getInt: function(a, b) {
        null == b && (b = 0);
        for (var c = 0, d = this.params; c < d.length;) {
            var f = d[c];
            ++c;
            if (f.id == a && f.type == jc.INT) return f.int
        }
        return b
    },
    getString: function(a, b) {
        for (var c = 0, d = this.params; c < d.length;) {
            var f = d[c];
            ++c;
            if (f.id == a && f.type == jc.STRING) return f.string
        }
        return b
    },
    getBool: function(a, b) {
        null == b && (b = !1);
        for (var c = 0, d = this.params; c < d.length;) {
            var f = d[c];
            ++c;
            if (f.id == a && f.type == jc.BOOL) return f.bool
        }
        return b
    },
    setColor: function(a, b) {
        this.params.push({
            id: a,
            type: jc.COLOR,
            color: b
        })
    },
    setMulti: function(a,
        b) {
        this.params.push({
            id: a,
            type: jc.MULTI,
            multi: b
        })
    },
    setFont: function(a, b) {
        this.params.push({
            id: a,
            type: jc.FONT,
            font: b
        })
    },
    setFloat: function(a, b) {
        this.params.push({
            id: a,
            type: jc.FLOAT,
            float: b
        })
    },
    setInt: function(a, b) {
        this.params.push({
            id: a,
            type: jc.INT,
            int: b
        })
    },
    setString: function(a, b) {
        this.params.push({
            id: a,
            type: jc.STRING,
            string: b
        })
    },
    setBool: function(a, b) {
        this.params.push({
            id: a,
            type: jc.BOOL,
            bool: b
        })
    },
    data: function() {
        for (var a = {}, b = 0, c = this.params; b < c.length;) {
            var d = c[b];
            ++b;
            switch (d.type._hx_index) {
                case 0:
                    a[d.id] =
                        "#" + O.hex(d.color, 6);
                    break;
                case 1:
                    var f = d.id,
                        h = [],
                        k = 0;
                    for (d = d.multi; k < d.length;) {
                        var n = d[k];
                        ++k;
                        h.push("#" + O.hex(n, 6))
                    }
                    a[f] = h;
                    break;
                case 2:
                    a[d.id] = d.font;
                    break;
                case 3:
                    a[d.id] = Xc.float2str(d.float);
                    break;
                case 4:
                    a[d.id] = null == d.int ? "null" : "" + d.int;
                    break;
                case 5:
                    a[d.id] = d.string;
                    break;
                case 6:
                    a[d.id] = null == d.bool ? "null" : "" + d.bool
            }
        }
        return a
    },
    json: function() {
        return JSON.stringify(this.data(), null, "  ")
    },
    __class__: Xc
};
var wd = function() {};
g["com.watabou.utils.PointExtender"] = wd;
wd.__name__ = "com.watabou.utils.PointExtender";
wd.set = function(a, b) {
    a.x = b.x;
    a.y = b.y
};
wd.project = function(a, b) {
    var c = a.get_length();
    return (a.x * b.x + a.y * b.y) / (c * c)
};
var gf = function() {};
g["com.watabou.utils.SetUtils"] = gf;
gf.__name__ = "com.watabou.utils.SetUtils";
gf.fromArray = function(a) {
    for (var b = new pa, c = 0; c < a.length;) {
        var d = a[c];
        ++c;
        b.set(d, !0)
    }
    return b
};
gf.removeArr = function(a, b) {
    for (var c = 0; c < b.length;) {
        var d = b[c];
        ++c;
        a.remove(d)
    }
};
gf.isEmpty = function(a) {
    for (a = a.iterator(); a.hasNext();) return a.next(), !1;
    return !0
};
var id = function() {};
g["com.watabou.utils.Stopwatch"] =
    id;
id.__name__ = "com.watabou.utils.Stopwatch";
id.start = function() {
    id.startTime = Ra.getTimer()
};
id.next = function() {
    var a = Ra.getTimer(),
        b = a - id.startTime;
    id.startTime = a;
    return b
};
id.measure = function(a) {
    id.start();
    a();
    return id.next()
};
var eh = function() {};
g["com.watabou.utils.StringUtils"] = eh;
eh.__name__ = "com.watabou.utils.StringUtils";
eh.capitalize = function(a) {
    return N.substr(a, 0, 1).toUpperCase() + N.substr(a, 1, null)
};
eh.capitalizeAll = function(a) {
    var b = [],
        c = 0;
    for (a = a.split(" "); c < a.length;) {
        var d = a[c];
        ++c;
        b.push(eh.capitalize(d))
    }
    return b.join(" ")
};
var rb = function() {};
g["com.watabou.utils.Updater"] = rb;
rb.__name__ = "com.watabou.utils.Updater";
rb.__properties__ = {
    get_tick: "get_tick"
};
rb.get_tick = function() {
    null == rb.source && rb.useTimer(60);
    return rb._tick
};
rb.fire = function() {
    var a = Ra.getTimer();
    0 == rb.lastTime ? rb._tick.dispatch(0) : rb._tick.dispatch((a - rb.lastTime) / 1E3 * rb.timeScale);
    rb.lastTime = a
};
rb.useTimer = function(a) {
    null != rb.source && rb.source.stop();
    rb.source = new Gi(a)
};
rb.useEnterFrame = function(a) {
    null !=
        rb.source && rb.source.stop();
    rb.source = new Hi(a)
};
rb.wait = function(a, b) {
    var c = 0,
        d = null;
    d = function(f) {
        (c += f) >= a && (rb.get_tick().remove(d), b())
    };
    rb.get_tick().add(d);
    return d
};
rb.cancel = function(a) {
    rb.get_tick().remove(a)
};
rb.stop = function() {
    null != rb.source && (rb.source.stop(), rb.source = null)
};
var yg = function() {};
g["com.watabou.utils.RecurringEventDispatcher"] = yg;
yg.__name__ = "com.watabou.utils.RecurringEventDispatcher";
yg.prototype = {
    stop: function() {},
    __class__: yg
};
var Gi = function(a) {
    this.timer = new Ii(a);
    this.timer.addEventListener("timer", l(this, this.onTimer));
    this.timer.start()
};
g["com.watabou.utils._Updater.TimerEventDispatcher"] = Gi;
Gi.__name__ = "com.watabou.utils._Updater.TimerEventDispatcher";
Gi.__super__ = yg;
Gi.prototype = v(yg.prototype, {
    onTimer: function(a) {
        rb.fire();
        a.updateAfterEvent()
    },
    stop: function() {
        this.timer.stop()
    },
    __class__: Gi
});
var Hi = function(a) {
    this.dispObj = a;
    a.addEventListener("enterFrame", l(this, this.onEnterFrame))
};
g["com.watabou.utils._Updater.FrameEventDispatcher"] = Hi;
Hi.__name__ =
    "com.watabou.utils._Updater.FrameEventDispatcher";
Hi.__super__ = yg;
Hi.prototype = v(yg.prototype, {
    onEnterFrame: function(a) {
        rb.fire()
    },
    stop: function() {
        this.dispObj.removeEventListener("enterFrame", l(this, this.onEnterFrame))
    },
    __class__: Hi
});
var ne = y["haxe.StackItem"] = {
    __ename__: "haxe.StackItem",
    __constructs__: null,
    CFunction: {
        _hx_name: "CFunction",
        _hx_index: 0,
        __enum__: "haxe.StackItem",
        toString: r
    },
    Module: (G = function(a) {
        return {
            _hx_index: 1,
            m: a,
            __enum__: "haxe.StackItem",
            toString: r
        }
    }, G._hx_name = "Module", G.__params__ = ["m"], G),
    FilePos: (G = function(a, b, c, d) {
        return {
            _hx_index: 2,
            s: a,
            file: b,
            line: c,
            column: d,
            __enum__: "haxe.StackItem",
            toString: r
        }
    }, G._hx_name = "FilePos", G.__params__ = ["s", "file", "line", "column"], G),
    Method: (G = function(a, b) {
        return {
            _hx_index: 3,
            classname: a,
            method: b,
            __enum__: "haxe.StackItem",
            toString: r
        }
    }, G._hx_name = "Method", G.__params__ = ["classname", "method"], G),
    LocalFunction: (G = function(a) {
        return {
            _hx_index: 4,
            v: a,
            __enum__: "haxe.StackItem",
            toString: r
        }
    }, G._hx_name = "LocalFunction", G.__params__ = ["v"], G)
};
ne.__constructs__ = [ne.CFunction, ne.Module, ne.FilePos, ne.Method, ne.LocalFunction];
var pf = {
        callStack: function() {
            return Ta.toHaxe(Ta.callStack())
        },
        exceptionStack: function(a) {
            null == a && (a = !1);
            var b = Ta.toHaxe(Ta.exceptionStack());
            return a ? b : pf.subtract(b, pf.callStack())
        },
        toString: function(a) {
            for (var b = new x, c = 0; c < a.length;) {
                var d = a[c];
                ++c;
                b.b += "\nCalled from ";
                pf.itemToString(b, d)
            }
            return b.b
        },
        subtract: function(a, b) {
            for (var c = -1, d = -1; ++d < a.length;) {
                for (var f = 0, h = b.length; f < h;) {
                    var k = f++;
                    if (pf.equalItems(a[d], b[k])) {
                        if (0 >
                            c && (c = d), ++d, d >= a.length) break
                    } else c = -1
                }
                if (0 <= c) break
            }
            return 0 <= c ? a.slice(0, c) : a
        },
        equalItems: function(a, b) {
            if (null == a) return null == b ? !0 : !1;
            switch (a._hx_index) {
                case 0:
                    return null == b ? !1 : 0 == b._hx_index ? !0 : !1;
                case 1:
                    return null == b ? !1 : 1 == b._hx_index ? a.m == b.m : !1;
                case 2:
                    if (null == b) return !1;
                    if (2 == b._hx_index) {
                        var c = b.s,
                            d = b.line,
                            f = b.column,
                            h = a.column,
                            k = a.line,
                            n = a.s;
                        return a.file == b.file && k == d && h == f ? pf.equalItems(n, c) : !1
                    }
                    return !1;
                case 3:
                    return null == b ? !1 : 3 == b._hx_index ? (c = b.method, d = a.method, a.classname ==
                        b.classname ? d == c : !1) : !1;
                case 4:
                    return null == b ? !1 : 4 == b._hx_index ? a.v == b.v : !1
            }
        },
        itemToString: function(a, b) {
            switch (b._hx_index) {
                case 0:
                    a.b += "a C function";
                    break;
                case 1:
                    b = b.m;
                    a.b += "module ";
                    a.b += null == b ? "null" : "" + b;
                    break;
                case 2:
                    var c = b.s,
                        d = b.file,
                        f = b.line;
                    b = b.column;
                    null != c && (pf.itemToString(a, c), a.b += " (");
                    a.b += null == d ? "null" : "" + d;
                    a.b += " line ";
                    a.b += null == f ? "null" : "" + f;
                    null != b && (a.b += " column ", a.b += null == b ? "null" : "" + b);
                    null != c && (a.b += ")");
                    break;
                case 3:
                    c = b.classname;
                    b = b.method;
                    a.b += H.string(null == c ?
                        "<unknown>" : c);
                    a.b += ".";
                    a.b += null == b ? "null" : "" + b;
                    break;
                case 4:
                    b = b.v, a.b += "local function #", a.b += null == b ? "null" : "" + b
            }
        }
    },
    X = function(a, b, c) {
        Error.call(this, a);
        this.message = a;
        this.__previousException = b;
        this.__nativeException = null != c ? c : this;
        this.__skipStack = 0;
        a = Error.prepareStackTrace;
        Error.prepareStackTrace = function(a) {
            return a.stack
        };
        if (c instanceof Error) this.stack = c.stack;
        else {
            c = null;
            if (Error.captureStackTrace) Error.captureStackTrace(this, X), c = this;
            else if (c = Error(), "undefined" == typeof c.stack) {
                try {
                    throw c;
                } catch (d) {}
                this.__skipStack++
            }
            this.stack = c.stack
        }
        Error.prepareStackTrace = a
    };
g["haxe.Exception"] = X;
X.__name__ = "haxe.Exception";
X.caught = function(a) {
    return a instanceof X ? a : a instanceof Error ? new X(a.message, null, a) : new rh(a, null, a)
};
X.thrown = function(a) {
    if (a instanceof X) return a.get_native();
    if (a instanceof Error) return a;
    a = new rh(a);
    a.__skipStack++;
    return a
};
X.__super__ = Error;
X.prototype = v(Error.prototype, {
    unwrap: function() {
        return this.__nativeException
    },
    toString: function() {
        return this.get_message()
    },
    __shiftStack: function() {
        this.__skipStack++
    },
    get_message: function() {
        return this.message
    },
    get_native: function() {
        return this.__nativeException
    },
    get_stack: function() {
        var a = this.__exceptionStack;
        null == a && (a = Ta.toHaxe(Ta.normalize(this.stack), this.__skipStack), this.setProperty("__exceptionStack", a));
        return a
    },
    setProperty: function(a, b) {
        try {
            Object.defineProperty(this, a, {
                value: b
            })
        } catch (c) {
            this[a] = b
        }
    },
    __class__: X,
    __properties__: {
        get_native: "get_native",
        get_stack: "get_stack",
        get_message: "get_message"
    }
});
var Da =
    function(a, b) {
        this.high = a;
        this.low = b
    };
g["haxe._Int64.___Int64"] = Da;
Da.__name__ = "haxe._Int64.___Int64";
Da.prototype = {
    __class__: Da
};
var hb = function() {};
g["haxe.Log"] = hb;
hb.__name__ = "haxe.Log";
hb.formatOutput = function(a, b) {
    var c = H.string(a);
    if (null == b) return c;
    var d = b.fileName + ":" + b.lineNumber;
    if (null != b.customParams) {
        var f = 0;
        for (b = b.customParams; f < b.length;) a = b[f], ++f, c += ", " + H.string(a)
    }
    return d + ": " + c
};
hb.trace = function(a, b) {
    a = hb.formatOutput(a, b);
    "undefined" != typeof console && null != console.log && console.log(a)
};
var Ta = function() {};
g["haxe.NativeStackTrace"] = Ta;
Ta.__name__ = "haxe.NativeStackTrace";
Ta.saveStack = function(a) {
    Ta.lastError = a
};
Ta.callStack = function() {
    var a = Error(""),
        b = Ta.tryHaxeStack(a);
    if ("undefined" == typeof b) {
        try {
            throw a;
        } catch (c) {}
        b = a.stack
    }
    return Ta.normalize(b, 2)
};
Ta.exceptionStack = function() {
    return Ta.normalize(Ta.tryHaxeStack(Ta.lastError))
};
Ta.toHaxe = function(a, b) {
    null == b && (b = 0);
    if (null == a) return [];
    if ("string" == typeof a) {
        a = a.split("\n");
        "Error" == a[0] && a.shift();
        for (var c = [], d = 0, f = a.length; d <
            f;) {
            var h = d++;
            if (!(b > h)) {
                var k = a[h];
                h = k.match(/^    at ([$A-Za-z0-9_. ]+) \(([^)]+):([0-9]+):([0-9]+)\)$/);
                if (null != h) {
                    k = h[1].split(".");
                    "$hxClasses" == k[0] && k.shift();
                    var n = k.pop(),
                        p = h[2],
                        g = H.parseInt(h[3]);
                    h = H.parseInt(h[4]);
                    c.push(ne.FilePos("Anonymous function" == n ? ne.LocalFunction() : "Global code" == n ? null : ne.Method(k.join("."), n), p, g, h))
                } else c.push(ne.Module(O.trim(k)))
            }
        }
        return c
    }
    return 0 < b && Array.isArray(a) ? a.slice(b) : a
};
Ta.tryHaxeStack = function(a) {
    if (null == a) return [];
    var b = Error.prepareStackTrace;
    Error.prepareStackTrace = Ta.prepareHxStackTrace;
    a = a.stack;
    Error.prepareStackTrace = b;
    return a
};
Ta.prepareHxStackTrace = function(a, b) {
    a = [];
    for (var c = 0; c < b.length;) {
        var d = b[c];
        ++c;
        null != Ta.wrapCallSite && (d = Ta.wrapCallSite(d));
        var f = null,
            h = d.getFunctionName();
        if (null != h) {
            var k = h.lastIndexOf(".");
            0 <= k ? (f = h.substring(0, k), h = h.substring(k + 1), f = ne.Method(f, h)) : f = ne.Method(null, h)
        }
        h = d.getFileName();
        k = null == h ? -1 : h.indexOf("file:");
        null != Ta.wrapCallSite && 0 < k && (h = h.substring(k + 6));
        a.push(ne.FilePos(f, h, d.getLineNumber(),
            d.getColumnNumber()))
    }
    return a
};
Ta.normalize = function(a, b) {
    null == b && (b = 0);
    if (Array.isArray(a) && 0 < b) return a.slice(b);
    if ("string" == typeof a) {
        switch (a.substring(0, 6)) {
            case "Error\n":
            case "Error:":
                ++b
        }
        return Ta.skipLines(a, b)
    }
    return a
};
Ta.skipLines = function(a, b, c) {
    null == c && (c = 0);
    return 0 < b ? (c = a.indexOf("\n", c), 0 > c ? "" : Ta.skipLines(a, --b, c + 1)) : a.substring(c)
};
var Bd = function() {
    this.buf = new x;
    this.cache = [];
    this.useCache = Bd.USE_CACHE;
    this.useEnumIndex = Bd.USE_ENUM_INDEX;
    this.shash = new Qa;
    this.scount = 0
};
g["haxe.Serializer"] =
    Bd;
Bd.__name__ = "haxe.Serializer";
Bd.run = function(a) {
    var b = new Bd;
    b.serialize(a);
    return b.toString()
};
Bd.prototype = {
    toString: function() {
        return this.buf.b
    },
    serializeString: function(a) {
        var b = this.shash.h[a];
        null != b ? (this.buf.b += "R", this.buf.b += null == b ? "null" : "" + b) : (this.shash.h[a] = this.scount++, this.buf.b += "y", a = encodeURIComponent(a), this.buf.b += H.string(a.length), this.buf.b += ":", this.buf.b += null == a ? "null" : "" + a)
    },
    serializeRef: function(a) {
        for (var b = typeof a, c = 0, d = this.cache.length; c < d;) {
            var f = c++,
                h = this.cache[f];
            if (typeof h == b && h == a) return this.buf.b += "r", this.buf.b += null == f ? "null" : "" + f, !0
        }
        this.cache.push(a);
        return !1
    },
    serializeFields: function(a) {
        for (var b = 0, c = ya.fields(a); b < c.length;) {
            var d = c[b];
            ++b;
            this.serializeString(d);
            this.serialize(ya.field(a, d))
        }
        this.buf.b += "g"
    },
    serialize: function(a) {
        var b = w.typeof(a);
        switch (b._hx_index) {
            case 0:
                this.buf.b += "n";
                break;
            case 1:
                if (0 == a) {
                    this.buf.b += "z";
                    break
                }
                this.buf.b += "i";
                this.buf.b += null == a ? "null" : "" + a;
                break;
            case 2:
                isNaN(a) ? this.buf.b += "k" : isFinite(a) ? (this.buf.b += "d",
                    this.buf.b += null == a ? "null" : "" + a) : this.buf.b += 0 > a ? "m" : "p";
                break;
            case 3:
                this.buf.b += a ? "t" : "f";
                break;
            case 4:
                va.__instanceof(a, xl) ? (a = a.__name__, this.buf.b += "A", this.serializeString(a)) : va.__instanceof(a, yl) ? (this.buf.b += "B", this.serializeString(a.__ename__)) : this.useCache && this.serializeRef(a) || (this.buf.b += "o", this.serializeFields(a));
                break;
            case 5:
                throw X.thrown("Cannot serialize function");
            case 6:
                b = b.c;
                if (b == String) {
                    this.serializeString(a);
                    break
                }
                if (this.useCache && this.serializeRef(a)) break;
                switch (b) {
                    case Array:
                        var c =
                            0;
                        this.buf.b += "a";
                        for (var d = 0, f = a.length; d < f;) b = d++, null == a[b] ? ++c : (0 < c && (1 == c ? this.buf.b += "n" : (this.buf.b += "u", this.buf.b += null == c ? "null" : "" + c), c = 0), this.serialize(a[b]));
                        0 < c && (1 == c ? this.buf.b += "n" : (this.buf.b += "u", this.buf.b += null == c ? "null" : "" + c));
                        this.buf.b += "h";
                        break;
                    case Date:
                        this.buf.b += "v";
                        this.buf.b += H.string(a.getTime());
                        break;
                    case mc:
                        this.buf.b += "q";
                        for (b = a.keys(); b.hasNext();) c = b.next(), this.buf.b += ":", this.buf.b += null == c ? "null" : "" + c, this.serialize(a.h[c]);
                        this.buf.b += "h";
                        break;
                    case ab:
                        this.buf.b +=
                            "l";
                        for (a = a.h; null != a;) b = a.item, a = a.next, this.serialize(b);
                        this.buf.b += "h";
                        break;
                    case pa:
                        this.buf.b += "M";
                        for (b = a.keys(); b.hasNext();) {
                            c = b.next();
                            var h = ya.field(c, "__id__");
                            ya.deleteField(c, "__id__");
                            this.serialize(c);
                            c.__id__ = h;
                            this.serialize(a.h[c.__id__])
                        }
                        this.buf.b += "h";
                        break;
                    case Qa:
                        this.buf.b += "b";
                        c = Object.keys(a.h);
                        h = c.length;
                        for (d = 0; d < h;) b = c[d++], this.serializeString(b), this.serialize(a.h[b]);
                        this.buf.b += "h";
                        break;
                    case zb:
                        this.buf.b += "s";
                        this.buf.b += H.string(Math.ceil(8 * a.length / 6));
                        this.buf.b +=
                            ":";
                        b = 0;
                        c = a.length - 2;
                        h = Bd.BASE64_CODES;
                        if (null == h) {
                            h = Array(Bd.BASE64.length);
                            d = 0;
                            for (f = Bd.BASE64.length; d < f;) {
                                var k = d++;
                                h[k] = N.cca(Bd.BASE64, k)
                            }
                            Bd.BASE64_CODES = h
                        }
                        for (; b < c;) d = a.b[b++], f = a.b[b++], k = a.b[b++], this.buf.b += String.fromCodePoint(h[d >> 2]), this.buf.b += String.fromCodePoint(h[(d << 4 | f >> 4) & 63]), this.buf.b += String.fromCodePoint(h[(f << 2 | k >> 6) & 63]), this.buf.b += String.fromCodePoint(h[k & 63]);
                        b == c ? (d = a.b[b++], f = a.b[b++], this.buf.b += String.fromCodePoint(h[d >> 2]), this.buf.b += String.fromCodePoint(h[(d <<
                            4 | f >> 4) & 63]), this.buf.b += String.fromCodePoint(h[f << 2 & 63])) : b == c + 1 && (d = a.b[b++], this.buf.b += String.fromCodePoint(h[d >> 2]), this.buf.b += String.fromCodePoint(h[d << 4 & 63]));
                        break;
                    default:
                        this.useCache && this.cache.pop(), null != a.hxSerialize ? (this.buf.b += "C", this.serializeString(b.__name__), this.useCache && this.cache.push(a), a.hxSerialize(this), this.buf.b += "g") : (this.buf.b += "c", this.serializeString(b.__name__), this.useCache && this.cache.push(a), this.serializeFields(a))
                }
                break;
            case 7:
                b = b.e;
                if (this.useCache) {
                    if (this.serializeRef(a)) break;
                    this.cache.pop()
                }
                this.buf.b += H.string(this.useEnumIndex ? "j" : "w");
                this.serializeString(b.__ename__);
                this.useEnumIndex ? (this.buf.b += ":", this.buf.b += H.string(a._hx_index)) : (b = a, this.serializeString(y[b.__enum__].__constructs__[b._hx_index]._hx_name));
                this.buf.b += ":";
                c = w.enumParameters(a);
                this.buf.b += H.string(c.length);
                for (b = 0; b < c.length;) h = c[b], ++b, this.serialize(h);
                this.useCache && this.cache.push(a);
                break;
            default:
                throw X.thrown("Cannot serialize " + H.string(a));
        }
    },
    __class__: Bd
};
var Qf = function(a) {
    var b =
        this;
    this.id = setInterval(function() {
        b.run()
    }, a)
};
g["haxe.Timer"] = Qf;
Qf.__name__ = "haxe.Timer";
Qf.delay = function(a, b) {
    var c = new Qf(b);
    c.run = function() {
        c.stop();
        a()
    };
    return c
};
Qf.prototype = {
    stop: function() {
        null != this.id && (clearInterval(this.id), this.id = null)
    },
    run: function() {},
    __class__: Qf
};
var Ji = function() {};
g["haxe._Unserializer.DefaultResolver"] = Ji;
Ji.__name__ = "haxe._Unserializer.DefaultResolver";
Ji.prototype = {
    resolveClass: function(a) {
        return g[a]
    },
    resolveEnum: function(a) {
        return y[a]
    },
    __class__: Ji
};
var pd = function(a) {
    this.buf = a;
    this.length = this.buf.length;
    this.pos = 0;
    this.scache = [];
    this.cache = [];
    a = pd.DEFAULT_RESOLVER;
    null == a && (a = new Ji, pd.DEFAULT_RESOLVER = a);
    this.resolver = a
};
g["haxe.Unserializer"] = pd;
pd.__name__ = "haxe.Unserializer";
pd.initCodes = function() {
    for (var a = [], b = 0, c = pd.BASE64.length; b < c;) {
        var d = b++;
        a[pd.BASE64.charCodeAt(d)] = d
    }
    return a
};
pd.run = function(a) {
    return (new pd(a)).unserialize()
};
pd.prototype = {
    setResolver: function(a) {
        null == a ? (null == zg.instance && (zg.instance = new zg), this.resolver =
            zg.instance) : this.resolver = a
    },
    readDigits: function() {
        for (var a = 0, b = !1, c = this.pos;;) {
            var d = this.buf.charCodeAt(this.pos);
            if (d != d) break;
            if (45 == d) {
                if (this.pos != c) break;
                b = !0
            } else {
                if (48 > d || 57 < d) break;
                a = 10 * a + (d - 48)
            }
            this.pos++
        }
        b && (a *= -1);
        return a
    },
    readFloat: function() {
        for (var a = this.pos;;) {
            var b = this.buf.charCodeAt(this.pos);
            if (b != b) break;
            if (43 <= b && 58 > b || 101 == b || 69 == b) this.pos++;
            else break
        }
        return parseFloat(N.substr(this.buf, a, this.pos - a))
    },
    unserializeObject: function(a) {
        for (;;) {
            if (this.pos >= this.length) throw X.thrown("Invalid object");
            if (103 == this.buf.charCodeAt(this.pos)) break;
            var b = this.unserialize();
            if ("string" != typeof b) throw X.thrown("Invalid object key");
            var c = this.unserialize();
            a[b] = c
        }
        this.pos++
    },
    unserializeEnum: function(a, b) {
        if (58 != this.buf.charCodeAt(this.pos++)) throw X.thrown("Invalid enum format");
        var c = this.readDigits();
        if (0 == c) return w.createEnum(a, b);
        for (var d = []; 0 < c--;) d.push(this.unserialize());
        return w.createEnum(a, b, d)
    },
    unserialize: function() {
        switch (this.buf.charCodeAt(this.pos++)) {
            case 65:
                var a = this.unserialize(),
                    b = this.resolver.resolveClass(a);
                if (null == b) throw X.thrown("Class not found " + a);
                return b;
            case 66:
                a = this.unserialize();
                b = this.resolver.resolveEnum(a);
                if (null == b) throw X.thrown("Enum not found " + a);
                return b;
            case 67:
                a = this.unserialize();
                b = this.resolver.resolveClass(a);
                if (null == b) throw X.thrown("Class not found " + a);
                b = Object.create(b.prototype);
                this.cache.push(b);
                b.hxUnserialize(this);
                if (103 != this.buf.charCodeAt(this.pos++)) throw X.thrown("Invalid custom data");
                return b;
            case 77:
                a = new pa;
                this.cache.push(a);
                for (var c; 104 != this.buf.charCodeAt(this.pos);) b = this.unserialize(), a.set(b, this.unserialize());
                this.pos++;
                return a;
            case 82:
                a = this.readDigits();
                if (0 > a || a >= this.scache.length) throw X.thrown("Invalid string reference");
                return this.scache[a];
            case 97:
                b = [];
                for (this.cache.push(b);;) {
                    c = this.buf.charCodeAt(this.pos);
                    if (104 == c) {
                        this.pos++;
                        break
                    }
                    117 == c ? (this.pos++, a = this.readDigits(), b[b.length + a - 1] = null) : b.push(this.unserialize())
                }
                return b;
            case 98:
                a = new Qa;
                for (this.cache.push(a); 104 != this.buf.charCodeAt(this.pos);) b =
                    this.unserialize(), c = this.unserialize(), a.h[b] = c;
                this.pos++;
                return a;
            case 99:
                a = this.unserialize();
                b = this.resolver.resolveClass(a);
                if (null == b) throw X.thrown("Class not found " + a);
                b = Object.create(b.prototype);
                this.cache.push(b);
                this.unserializeObject(b);
                return b;
            case 100:
                return this.readFloat();
            case 102:
                return !1;
            case 105:
                return this.readDigits();
            case 106:
                a = this.unserialize();
                c = this.resolver.resolveEnum(a);
                if (null == c) throw X.thrown("Enum not found " + a);
                this.pos++;
                for (var d = this.readDigits(), f = c.__constructs__,
                        h = Array(f.length), k = 0, n = f.length; k < n;) b = k++, h[b] = f[b]._hx_name;
                b = h[d];
                if (null == b) throw X.thrown("Unknown enum index " + a + "@" + d);
                b = this.unserializeEnum(c, b);
                this.cache.push(b);
                return b;
            case 107:
                return NaN;
            case 108:
                b = new ab;
                for (this.cache.push(b); 104 != this.buf.charCodeAt(this.pos);) b.add(this.unserialize());
                this.pos++;
                return b;
            case 109:
                return -Infinity;
            case 110:
                return null;
            case 111:
                return b = {}, this.cache.push(b), this.unserializeObject(b), b;
            case 112:
                return Infinity;
            case 113:
                a = new mc;
                this.cache.push(a);
                for (c = this.buf.charCodeAt(this.pos++); 58 == c;) b = this.readDigits(), c = this.unserialize(), a.h[b] = c, c = this.buf.charCodeAt(this.pos++);
                if (104 != c) throw X.thrown("Invalid IntMap format");
                return a;
            case 114:
                a = this.readDigits();
                if (0 > a || a >= this.cache.length) throw X.thrown("Invalid reference");
                return this.cache[a];
            case 115:
                a = this.readDigits();
                c = this.buf;
                if (58 != this.buf.charCodeAt(this.pos++) || this.length - this.pos < a) throw X.thrown("Invalid bytes length");
                d = pd.CODES;
                null == d && (d = pd.initCodes(), pd.CODES = d);
                b = this.pos;
                f = a & 3;
                h = b + (a - f);
                k = new zb(new ArrayBuffer(3 * (a >> 2) + (2 <= f ? f - 1 : 0)));
                for (n = 0; b < h;) {
                    var p = d[c.charCodeAt(b++)],
                        g = d[c.charCodeAt(b++)];
                    k.b[n++] = (p << 2 | g >> 4) & 255;
                    p = d[c.charCodeAt(b++)];
                    k.b[n++] = (g << 4 | p >> 2) & 255;
                    g = d[c.charCodeAt(b++)];
                    k.b[n++] = (p << 6 | g) & 255
                }
                2 <= f && (p = d[c.charCodeAt(b++)], g = d[c.charCodeAt(b++)], k.b[n++] = (p << 2 | g >> 4) & 255, 3 == f && (p = d[c.charCodeAt(b++)], k.b[n++] = (g << 4 | p >> 2) & 255));
                this.pos += a;
                this.cache.push(k);
                return k;
            case 116:
                return !0;
            case 118:
                return 48 <= this.buf.charCodeAt(this.pos) && 57 >= this.buf.charCodeAt(this.pos) &&
                    48 <= this.buf.charCodeAt(this.pos + 1) && 57 >= this.buf.charCodeAt(this.pos + 1) && 48 <= this.buf.charCodeAt(this.pos + 2) && 57 >= this.buf.charCodeAt(this.pos + 2) && 48 <= this.buf.charCodeAt(this.pos + 3) && 57 >= this.buf.charCodeAt(this.pos + 3) && 45 == this.buf.charCodeAt(this.pos + 4) ? (b = N.strDate(N.substr(this.buf, this.pos, 19)), this.pos += 19) : b = new Date(this.readFloat()), this.cache.push(b), b;
            case 119:
                a = this.unserialize();
                c = this.resolver.resolveEnum(a);
                if (null == c) throw X.thrown("Enum not found " + a);
                b = this.unserializeEnum(c, this.unserialize());
                this.cache.push(b);
                return b;
            case 120:
                throw X.thrown(this.unserialize());
            case 121:
                a = this.readDigits();
                if (58 != this.buf.charCodeAt(this.pos++) || this.length - this.pos < a) throw X.thrown("Invalid string length");
                b = N.substr(this.buf, this.pos, a);
                this.pos += a;
                b = decodeURIComponent(b.split("+").join(" "));
                this.scache.push(b);
                return b;
            case 122:
                return 0
        }
        this.pos--;
        throw X.thrown("Invalid char " + this.buf.charAt(this.pos) + " at position " + this.pos);
    },
    __class__: pd
};
var zg = function() {};
g["haxe._Unserializer.NullResolver"] =
    zg;
zg.__name__ = "haxe._Unserializer.NullResolver";
zg.prototype = {
    resolveClass: function(a) {
        return null
    },
    resolveEnum: function(a) {
        return null
    },
    __class__: zg
};
var rh = function(a, b, c) {
    X.call(this, String(a), b, c);
    this.value = a;
    this.__skipStack++
};
g["haxe.ValueException"] = rh;
rh.__name__ = "haxe.ValueException";
rh.__super__ = X;
rh.prototype = v(X.prototype, {
    unwrap: function() {
        return this.value
    },
    __class__: rh
});
var Ag = function() {
    this.a1 = 1;
    this.a2 = 0
};
g["haxe.crypto.Adler32"] = Ag;
Ag.__name__ = "haxe.crypto.Adler32";
Ag.read =
    function(a) {
        var b = new Ag,
            c = a.readByte(),
            d = a.readByte(),
            f = a.readByte();
        a = a.readByte();
        b.a1 = f << 8 | a;
        b.a2 = c << 8 | d;
        return b
    };
Ag.prototype = {
    update: function(a, b, c) {
        var d = this.a1,
            f = this.a2,
            h = b;
        for (b += c; h < b;) c = h++, d = (d + a.b[c]) % 65521, f = (f + d) % 65521;
        this.a1 = d;
        this.a2 = f
    },
    equals: function(a) {
        return a.a1 == this.a1 ? a.a2 == this.a2 : !1
    },
    __class__: Ag
};
var zb = function(a) {
    this.length = a.byteLength;
    this.b = new Uint8Array(a);
    this.b.bufferValue = a;
    a.hxBytes = this;
    a.bytes = this.b
};
g["haxe.io.Bytes"] = zb;
zb.__name__ = "haxe.io.Bytes";
zb.ofString = function(a, b) {
    b = [];
    for (var c = 0; c < a.length;) {
        var d = a.charCodeAt(c++);
        55296 <= d && 56319 >= d && (d = d - 55232 << 10 | a.charCodeAt(c++) & 1023);
        127 >= d ? b.push(d) : (2047 >= d ? b.push(192 | d >> 6) : (65535 >= d ? b.push(224 | d >> 12) : (b.push(240 | d >> 18), b.push(128 | d >> 12 & 63)), b.push(128 | d >> 6 & 63)), b.push(128 | d & 63))
    }
    return new zb((new Uint8Array(b)).buffer)
};
zb.ofData = function(a) {
    var b = a.hxBytes;
    return null != b ? b : new zb(a)
};
zb.prototype = {
    blit: function(a, b, c, d) {
        if (0 > a || 0 > c || 0 > d || a + d > this.length || c + d > b.length) throw X.thrown(oe.OutsideBounds);
        0 == c && d == b.b.byteLength ? this.b.set(b.b, a) : this.b.set(b.b.subarray(c, c + d), a)
    },
    setUInt16: function(a, b) {
        null == this.data && (this.data = new DataView(this.b.buffer, this.b.byteOffset, this.b.byteLength));
        this.data.setUint16(a, b, !0)
    },
    setInt32: function(a, b) {
        null == this.data && (this.data = new DataView(this.b.buffer, this.b.byteOffset, this.b.byteLength));
        this.data.setInt32(a, b, !0)
    },
    getString: function(a, b, c) {
        if (0 > a || 0 > b || a + b > this.length) throw X.thrown(oe.OutsideBounds);
        c = "";
        var d = this.b,
            f = wb.fromCharCode,
            h = a;
        for (a +=
            b; h < a;)
            if (b = d[h++], 128 > b) {
                if (0 == b) break;
                c += f(b)
            } else if (224 > b) c += f((b & 63) << 6 | d[h++] & 127);
        else if (240 > b) {
            var k = d[h++];
            c += f((b & 31) << 12 | (k & 127) << 6 | d[h++] & 127)
        } else {
            k = d[h++];
            var n = d[h++];
            b = (b & 15) << 18 | (k & 127) << 12 | (n & 127) << 6 | d[h++] & 127;
            c += f((b >> 10) + 55232);
            c += f(b & 1023 | 56320)
        }
        return c
    },
    toString: function() {
        return this.getString(0, this.length)
    },
    __class__: zb
};
var Jc = function() {};
g["haxe.ds.ArraySort"] = Jc;
Jc.__name__ = "haxe.ds.ArraySort";
Jc.sort = function(a, b) {
    Jc.rec(a, b, 0, a.length)
};
Jc.rec = function(a, b, c, d) {
    var f =
        c + d >> 1;
    if (12 > d - c) {
        if (!(d <= c))
            for (f = c + 1; f < d;)
                for (var h = f++; h > c;) {
                    if (0 > b(a[h], a[h - 1])) Jc.swap(a, h - 1, h);
                    else break;
                    --h
                }
    } else Jc.rec(a, b, c, f), Jc.rec(a, b, f, d), Jc.doMerge(a, b, c, f, d, f - c, d - f)
};
Jc.doMerge = function(a, b, c, d, f, h, k) {
    if (0 != h && 0 != k)
        if (2 == h + k) 0 > b(a[d], a[c]) && Jc.swap(a, d, c);
        else {
            if (h > k) {
                var n = h >> 1;
                var p = c + n;
                var g = Jc.lower(a, b, d, f, p);
                var q = g - d
            } else q = k >> 1, g = d + q, p = Jc.upper(a, b, c, d, g), n = p - c;
            Jc.rotate(a, b, p, d, g);
            d = p + q;
            Jc.doMerge(a, b, c, p, d, n, q);
            Jc.doMerge(a, b, d, g, f, h - n, k - q)
        }
};
Jc.rotate = function(a, b, c,
    d, f) {
    if (c != d && d != f)
        for (b = Jc.gcd(f - c, d - c); 0 != b--;) {
            for (var h = a[c + b], k = d - c, n = c + b, p = c + b + k; p != c + b;) a[n] = a[p], n = p, p = f - p > k ? p + k : c + (k - (f - p));
            a[n] = h
        }
};
Jc.gcd = function(a, b) {
    for (; 0 != b;) {
        var c = a % b;
        a = b;
        b = c
    }
    return a
};
Jc.upper = function(a, b, c, d, f) {
    d -= c;
    for (var h, k; 0 < d;) h = d >> 1, k = c + h, 0 > b(a[f], a[k]) ? d = h : (c = k + 1, d = d - h - 1);
    return c
};
Jc.lower = function(a, b, c, d, f) {
    d -= c;
    for (var h, k; 0 < d;) h = d >> 1, k = c + h, 0 > b(a[k], a[f]) ? (c = k + 1, d = d - h - 1) : d = h;
    return c
};
Jc.swap = function(a, b, c) {
    var d = a[b];
    a[b] = a[c];
    a[c] = d
};
var mc = function() {
    this.h = {}
};
g["haxe.ds.IntMap"] = mc;
mc.__name__ = "haxe.ds.IntMap";
mc.__interfaces__ = [Y];
mc.prototype = {
    set: function(a, b) {
        this.h[a] = b
    },
    get: function(a) {
        return this.h[a]
    },
    remove: function(a) {
        if (!this.h.hasOwnProperty(a)) return !1;
        delete this.h[a];
        return !0
    },
    keys: function() {
        var a = [],
            b;
        for (b in this.h) this.h.hasOwnProperty(b) && a.push(+b);
        return new yf(a)
    },
    iterator: function() {
        return {
            ref: this.h,
            it: this.keys(),
            hasNext: function() {
                return this.it.hasNext()
            },
            next: function() {
                var a = this.it.next();
                return this.ref[a]
            }
        }
    },
    __class__: mc
};
var Mh = function(a, b) {
    this.item = a;
    this.next = b
};
g["haxe.ds._List.ListNode"] = Mh;
Mh.__name__ = "haxe.ds._List.ListNode";
Mh.prototype = {
    __class__: Mh
};
var Uj = function(a) {
    this.head = a
};
g["haxe.ds._List.ListIterator"] = Uj;
Uj.__name__ = "haxe.ds._List.ListIterator";
Uj.prototype = {
    hasNext: function() {
        return null != this.head
    },
    next: function() {
        var a = this.head.item;
        this.head = this.head.next;
        return a
    },
    __class__: Uj
};
var Qa = function() {
    this.h = Object.create(null)
};
g["haxe.ds.StringMap"] = Qa;
Qa.__name__ = "haxe.ds.StringMap";
Qa.__interfaces__ = [Y];
Qa.prototype = {
    get: function(a) {
        return this.h[a]
    },
    set: function(a, b) {
        this.h[a] = b
    },
    remove: function(a) {
        return Object.prototype.hasOwnProperty.call(this.h, a) ? (delete this.h[a], !0) : !1
    },
    keys: function() {
        return new Ph(this.h)
    },
    __class__: Qa
};
var Ph = function(a) {
    this.h = a;
    this.keys = Object.keys(a);
    this.length = this.keys.length;
    this.current = 0
};
g["haxe.ds._StringMap.StringMapKeyIterator"] = Ph;
Ph.__name__ = "haxe.ds._StringMap.StringMapKeyIterator";
Ph.prototype = {
    hasNext: function() {
        return this.current < this.length
    },
    next: function() {
        return this.keys[this.current++]
    },
    __class__: Ph
};
var Bg = function(a, b, c) {
    X.call(this, a, b);
    this.posInfos = null == c ? {
        fileName: "(unknown)",
        lineNumber: 0,
        className: "(unknown)",
        methodName: "(unknown)"
    } : c;
    this.__skipStack++
};
g["haxe.exceptions.PosException"] = Bg;
Bg.__name__ = "haxe.exceptions.PosException";
Bg.__super__ = X;
Bg.prototype = v(X.prototype, {
    toString: function() {
        return "" + X.prototype.toString.call(this) + " in " + this.posInfos.className + "." + this.posInfos.methodName + " at " + this.posInfos.fileName +
            ":" + this.posInfos.lineNumber
    },
    __class__: Bg
});
var Ki = function(a, b, c) {
    null == a && (a = "Not implemented");
    Bg.call(this, a, b, c);
    this.__skipStack++
};
g["haxe.exceptions.NotImplementedException"] = Ki;
Ki.__name__ = "haxe.exceptions.NotImplementedException";
Ki.__super__ = Bg;
Ki.prototype = v(Bg.prototype, {
    __class__: Ki
});
var Li = function() {
    this.size = this.pos = 0
};
g["haxe.io.BytesBuffer"] = Li;
Li.__name__ = "haxe.io.BytesBuffer";
Li.prototype = {
    addByte: function(a) {
        this.pos == this.size && this.grow(1);
        this.view.setUint8(this.pos++,
            a)
    },
    add: function(a) {
        this.pos + a.length > this.size && this.grow(a.length);
        if (0 != this.size) {
            var b = new Uint8Array(a.b.buffer, a.b.byteOffset, a.length);
            this.u8.set(b, this.pos);
            this.pos += a.length
        }
    },
    addBytes: function(a, b, c) {
        if (0 > b || 0 > c || b + c > a.length) throw X.thrown(oe.OutsideBounds);
        this.pos + c > this.size && this.grow(c);
        0 != this.size && (a = new Uint8Array(a.b.buffer, a.b.byteOffset + b, c), this.u8.set(a, this.pos), this.pos += c)
    },
    grow: function(a) {
        var b = this.pos + a;
        for (a = 0 == this.size ? 16 : this.size; a < b;) a = 3 * a >> 1;
        b = new ArrayBuffer(a);
        var c = new Uint8Array(b);
        0 < this.size && c.set(this.u8);
        this.size = a;
        this.buffer = b;
        this.u8 = c;
        this.view = new DataView(this.buffer)
    },
    getBytes: function() {
        if (0 == this.size) return new zb(new ArrayBuffer(0));
        var a = new zb(this.buffer);
        a.length = this.pos;
        return a
    },
    __class__: Li
};
var Mi = function() {};
g["haxe.io.Input"] = Mi;
Mi.__name__ = "haxe.io.Input";
Mi.prototype = {
    readByte: function() {
        throw new Ki(null, null, {
            fileName: "haxe/io/Input.hx",
            lineNumber: 53,
            className: "haxe.io.Input",
            methodName: "readByte"
        });
    },
    readBytes: function(a,
        b, c) {
        var d = c,
            f = a.b;
        if (0 > b || 0 > c || b + c > a.length) throw X.thrown(oe.OutsideBounds);
        try {
            for (; 0 < d;) f[b] = this.readByte(), ++b, --d
        } catch (h) {
            if (Ta.lastError = h, !(X.caught(h).unwrap() instanceof sh)) throw h;
        }
        return c - d
    },
    readFullBytes: function(a, b, c) {
        for (; 0 < c;) {
            var d = this.readBytes(a, b, c);
            if (0 == d) throw X.thrown(oe.Blocked);
            b += d;
            c -= d
        }
    },
    read: function(a) {
        for (var b = new zb(new ArrayBuffer(a)), c = 0; 0 < a;) {
            var d = this.readBytes(b, c, a);
            if (0 == d) throw X.thrown(oe.Blocked);
            c += d;
            a -= d
        }
        return b
    },
    readInt16: function() {
        var a = this.readByte(),
            b = this.readByte();
        a = this.bigEndian ? b | a << 8 : a | b << 8;
        return 0 != (a & 32768) ? a - 65536 : a
    },
    readUInt16: function() {
        var a = this.readByte(),
            b = this.readByte();
        return this.bigEndian ? b | a << 8 : a | b << 8
    },
    readInt32: function() {
        var a = this.readByte(),
            b = this.readByte(),
            c = this.readByte(),
            d = this.readByte();
        return this.bigEndian ? d | c << 8 | b << 16 | a << 24 : a | b << 8 | c << 16 | d << 24
    },
    readString: function(a, b) {
        var c = new zb(new ArrayBuffer(a));
        this.readFullBytes(c, 0, a);
        return c.getString(0, a, b)
    },
    __class__: Mi
};
var Ni = function(a, b, c) {
    null == b && (b = 0);
    null ==
        c && (c = a.length - b);
    if (0 > b || 0 > c || b + c > a.length) throw X.thrown(oe.OutsideBounds);
    this.b = a.b;
    this.pos = b;
    this.totlen = this.len = c
};
g["haxe.io.BytesInput"] = Ni;
Ni.__name__ = "haxe.io.BytesInput";
Ni.__super__ = Mi;
Ni.prototype = v(Mi.prototype, {
    readByte: function() {
        if (0 == this.len) throw X.thrown(new sh);
        this.len--;
        return this.b[this.pos++]
    },
    readBytes: function(a, b, c) {
        if (0 > b || 0 > c || b + c > a.length) throw X.thrown(oe.OutsideBounds);
        if (0 == this.len && 0 < c) throw X.thrown(new sh);
        this.len < c && (c = this.len);
        var d = this.b;
        a = a.b;
        for (var f =
                0, h = c; f < h;) {
            var k = f++;
            a[b + k] = d[this.pos + k]
        }
        this.pos += c;
        this.len -= c;
        return c
    },
    __class__: Ni
});
var wl = y["haxe.io.Encoding"] = {
    __ename__: "haxe.io.Encoding",
    __constructs__: null,
    UTF8: {
        _hx_name: "UTF8",
        _hx_index: 0,
        __enum__: "haxe.io.Encoding",
        toString: r
    },
    RawNative: {
        _hx_name: "RawNative",
        _hx_index: 1,
        __enum__: "haxe.io.Encoding",
        toString: r
    }
};
wl.__constructs__ = [wl.UTF8, wl.RawNative];
var sh = function() {};
g["haxe.io.Eof"] = sh;
sh.__name__ = "haxe.io.Eof";
sh.prototype = {
    toString: function() {
        return "Eof"
    },
    __class__: sh
};
var oe =
    y["haxe.io.Error"] = {
        __ename__: "haxe.io.Error",
        __constructs__: null,
        Blocked: {
            _hx_name: "Blocked",
            _hx_index: 0,
            __enum__: "haxe.io.Error",
            toString: r
        },
        Overflow: {
            _hx_name: "Overflow",
            _hx_index: 1,
            __enum__: "haxe.io.Error",
            toString: r
        },
        OutsideBounds: {
            _hx_name: "OutsideBounds",
            _hx_index: 2,
            __enum__: "haxe.io.Error",
            toString: r
        },
        Custom: (G = function(a) {
            return {
                _hx_index: 3,
                e: a,
                __enum__: "haxe.io.Error",
                toString: r
            }
        }, G._hx_name = "Custom", G.__params__ = ["e"], G)
    };
oe.__constructs__ = [oe.Blocked, oe.Overflow, oe.OutsideBounds, oe.Custom];
var Jl = {
        fromBytes: function(a, b, c) {
            null == b && (b = 0);
            null == c && (c = a.length - b >> 2);
            return new Float32Array(a.b.bufferValue, b, c)
        }
    },
    Ud = function(a) {
        switch (a) {
            case ".":
            case "..":
                this.dir = a;
                this.file = "";
                return
        }
        var b = a.lastIndexOf("/"),
            c = a.lastIndexOf("\\");
        b < c ? (this.dir = N.substr(a, 0, c), a = N.substr(a, c + 1, null), this.backslash = !0) : c < b ? (this.dir = N.substr(a, 0, b), a = N.substr(a, b + 1, null)) : this.dir = null;
        b = a.lastIndexOf("."); - 1 != b ? (this.ext = N.substr(a, b + 1, null), this.file = N.substr(a, 0, b)) : (this.ext = null, this.file = a)
    };
g["haxe.io.Path"] =
    Ud;
Ud.__name__ = "haxe.io.Path";
Ud.withoutDirectory = function(a) {
    a = new Ud(a);
    a.dir = null;
    return a.toString()
};
Ud.directory = function(a) {
    a = new Ud(a);
    return null == a.dir ? "" : a.dir
};
Ud.extension = function(a) {
    a = new Ud(a);
    return null == a.ext ? "" : a.ext
};
Ud.prototype = {
    toString: function() {
        return (null == this.dir ? "" : this.dir + (this.backslash ? "\\" : "/")) + this.file + (null == this.ext ? "" : "." + this.ext)
    },
    __class__: Ud
};
var yf = function(a) {
    this.current = 0;
    this.array = a
};
g["haxe.iterators.ArrayIterator"] = yf;
yf.__name__ = "haxe.iterators.ArrayIterator";
yf.prototype = {
    hasNext: function() {
        return this.current < this.array.length
    },
    next: function() {
        return this.array[this.current++]
    },
    __class__: yf
};
var Kc = function(a, b, c) {
    this.xml = b;
    this.message = a;
    this.position = c;
    this.lineNumber = 1;
    for (a = this.positionAtLine = 0; a < c;) {
        var d = a++;
        d = b.charCodeAt(d);
        10 == d ? (this.lineNumber++, this.positionAtLine = 0) : 13 != d && this.positionAtLine++
    }
};
g["haxe.xml.XmlParserException"] = Kc;
Kc.__name__ = "haxe.xml.XmlParserException";
Kc.prototype = {
    toString: function() {
        return va.getClass(this).__name__ +
            ": " + this.message + " at line " + this.lineNumber + " char " + this.positionAtLine
    },
    __class__: Kc
};
var ef = function() {};
g["haxe.xml.Parser"] = ef;
ef.__name__ = "haxe.xml.Parser";
ef.parse = function(a, b) {
    null == b && (b = !1);
    var c = W.createDocument();
    ef.doParse(a, b, 0, c);
    return c
};
ef.doParse = function(a, b, c, d) {
    null == c && (c = 0);
    for (var f = null, h = 1, k = 1, n = null, p = 0, g = 0, q = 0, m = new x, u = 1, r = -1; c < a.length;) {
        var l = a.charCodeAt(c);
        switch (h) {
            case 0:
                switch (l) {
                    case 9:
                    case 10:
                    case 13:
                    case 32:
                        break;
                    default:
                        h = k;
                        continue
                }
                break;
            case 1:
                if (60 ==
                    l) h = 0, k = 2;
                else {
                    p = c;
                    h = 13;
                    continue
                }
                break;
            case 2:
                switch (l) {
                    case 33:
                        if (91 == a.charCodeAt(c + 1)) {
                            c += 2;
                            if ("CDATA[" != N.substr(a, c, 6).toUpperCase()) throw X.thrown(new Kc("Expected <![CDATA[", a, c));
                            c += 5;
                            h = 17
                        } else if (68 == a.charCodeAt(c + 1) || 100 == a.charCodeAt(c + 1)) {
                            if ("OCTYPE" != N.substr(a, c + 2, 6).toUpperCase()) throw X.thrown(new Kc("Expected <!DOCTYPE", a, c));
                            c += 8;
                            h = 16
                        } else {
                            if (45 != a.charCodeAt(c + 1) || 45 != a.charCodeAt(c + 2)) throw X.thrown(new Kc("Expected \x3c!--", a, c));
                            c += 2;
                            h = 15
                        }
                        p = c + 1;
                        break;
                    case 47:
                        if (null == d) throw X.thrown(new Kc("Expected node name",
                            a, c));
                        p = c + 1;
                        h = 0;
                        k = 10;
                        break;
                    case 63:
                        h = 14;
                        p = c;
                        break;
                    default:
                        h = 3;
                        p = c;
                        continue
                }
                break;
            case 3:
                if (!(97 <= l && 122 >= l || 65 <= l && 90 >= l || 48 <= l && 57 >= l || 58 == l || 46 == l || 95 == l || 45 == l)) {
                    if (c == p) throw X.thrown(new Kc("Expected node name", a, c));
                    f = W.createElement(N.substr(a, p, c - p));
                    d.addChild(f);
                    ++g;
                    h = 0;
                    k = 4;
                    continue
                }
                break;
            case 4:
                switch (l) {
                    case 47:
                        h = 11;
                        break;
                    case 62:
                        h = 9;
                        break;
                    default:
                        h = 5;
                        p = c;
                        continue
                }
                break;
            case 5:
                if (!(97 <= l && 122 >= l || 65 <= l && 90 >= l || 48 <= l && 57 >= l || 58 == l || 46 == l || 95 == l || 45 == l)) {
                    if (p == c) throw X.thrown(new Kc("Expected attribute name",
                        a, c));
                    n = N.substr(a, p, c - p);
                    if (f.exists(n)) throw X.thrown(new Kc("Duplicate attribute [" + n + "]", a, c));
                    h = 0;
                    k = 6;
                    continue
                }
                break;
            case 6:
                if (61 == l) h = 0, k = 7;
                else throw X.thrown(new Kc("Expected =", a, c));
                break;
            case 7:
                switch (l) {
                    case 34:
                    case 39:
                        m = new x;
                        h = 8;
                        p = c + 1;
                        r = l;
                        break;
                    default:
                        throw X.thrown(new Kc('Expected "', a, c));
                }
                break;
            case 8:
                switch (l) {
                    case 38:
                        u = c - p;
                        m.b += null == u ? N.substr(a, p, null) : N.substr(a, p, u);
                        h = 18;
                        u = 8;
                        p = c + 1;
                        break;
                    case 60:
                    case 62:
                        if (b) throw X.thrown(new Kc("Invalid unescaped " + String.fromCodePoint(l) +
                            " in attribute value", a, c));
                        l == r && (k = c - p, m.b += null == k ? N.substr(a, p, null) : N.substr(a, p, k), k = m.b, m = new x, f.set(n, k), h = 0, k = 4);
                        break;
                    default:
                        l == r && (k = c - p, m.b += null == k ? N.substr(a, p, null) : N.substr(a, p, k), k = m.b, m = new x, f.set(n, k), h = 0, k = 4)
                }
                break;
            case 9:
                p = c = ef.doParse(a, b, c, f);
                h = 1;
                break;
            case 10:
                if (!(97 <= l && 122 >= l || 65 <= l && 90 >= l || 48 <= l && 57 >= l || 58 == l || 46 == l || 95 == l || 45 == l)) {
                    if (p == c) throw X.thrown(new Kc("Expected node name", a, c));
                    k = N.substr(a, p, c - p);
                    if (null == d || 0 != d.nodeType) throw X.thrown(new Kc("Unexpected </" +
                        k + ">, tag is not open", a, c));
                    if (d.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == d.nodeType ? "null" : Cb.toString(d.nodeType)));
                    if (k != d.nodeName) {
                        if (d.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == d.nodeType ? "null" : Cb.toString(d.nodeType)));
                        throw X.thrown(new Kc("Expected </" + d.nodeName + ">", a, c));
                    }
                    h = 0;
                    k = 12;
                    continue
                }
                break;
            case 11:
                if (62 == l) h = 1;
                else throw X.thrown(new Kc("Expected >", a, c));
                break;
            case 12:
                if (62 == l) return 0 ==
                    g && d.addChild(W.createPCData("")), c;
                throw X.thrown(new Kc("Expected >", a, c));
            case 13:
                60 == l ? (k = c - p, m.b += null == k ? N.substr(a, p, null) : N.substr(a, p, k), k = W.createPCData(m.b), m = new x, d.addChild(k), ++g, h = 0, k = 2) : 38 == l && (u = c - p, m.b += null == u ? N.substr(a, p, null) : N.substr(a, p, u), h = 18, u = 13, p = c + 1);
                break;
            case 14:
                63 == l && 62 == a.charCodeAt(c + 1) && (++c, h = N.substr(a, p + 1, c - p - 2), d.addChild(W.createProcessingInstruction(h)), ++g, h = 1);
                break;
            case 15:
                45 == l && 45 == a.charCodeAt(c + 1) && 62 == a.charCodeAt(c + 2) && (d.addChild(W.createComment(N.substr(a,
                    p, c - p))), ++g, c += 2, h = 1);
                break;
            case 16:
                91 == l ? ++q : 93 == l ? --q : 62 == l && 0 == q && (d.addChild(W.createDocType(N.substr(a, p, c - p))), ++g, h = 1);
                break;
            case 17:
                93 == l && 93 == a.charCodeAt(c + 1) && 62 == a.charCodeAt(c + 2) && (h = W.createCData(N.substr(a, p, c - p)), d.addChild(h), ++g, c += 2, h = 1);
                break;
            case 18:
                if (59 == l) {
                    p = N.substr(a, p, c - p);
                    if (35 == p.charCodeAt(0)) p = 120 == p.charCodeAt(1) ? H.parseInt("0" + N.substr(p, 1, p.length - 1)) : H.parseInt(N.substr(p, 1, p.length - 1)), m.b += String.fromCodePoint(p);
                    else if (Object.prototype.hasOwnProperty.call(ef.escapes.h,
                            p)) m.b += H.string(ef.escapes.h[p]);
                    else {
                        if (b) throw X.thrown(new Kc("Undefined entity: " + p, a, c));
                        m.b += H.string("&" + p + ";")
                    }
                    p = c + 1;
                    h = u
                } else if (!(97 <= l && 122 >= l || 65 <= l && 90 >= l || 48 <= l && 57 >= l || 58 == l || 46 == l || 95 == l || 45 == l) && 35 != l) {
                    if (b) throw X.thrown(new Kc("Invalid character in entity: " + String.fromCodePoint(l), a, c));
                    m.b += String.fromCodePoint(38);
                    h = c - p;
                    m.b += null == h ? N.substr(a, p, null) : N.substr(a, p, h);
                    --c;
                    p = c + 1;
                    h = u
                }
        }++c
    }
    1 == h && (p = c, h = 13);
    if (13 == h) {
        if (0 == d.nodeType) {
            if (d.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " +
                (null == d.nodeType ? "null" : Cb.toString(d.nodeType)));
            throw X.thrown(new Kc("Unclosed node <" + d.nodeName + ">", a, c));
        }
        if (c != p || 0 == g) u = c - p, m.b += null == u ? N.substr(a, p, null) : N.substr(a, p, u), d.addChild(W.createPCData(m.b));
        return c
    }
    if (!b && 18 == h && 13 == u) return m.b += String.fromCodePoint(38), u = c - p, m.b += null == u ? N.substr(a, p, null) : N.substr(a, p, u), d.addChild(W.createPCData(m.b)), c;
    throw X.thrown(new Kc("Unexpected end", a, c));
};
var Df = function(a) {
    this.output = new x;
    this.pretty = a
};
g["haxe.xml.Printer"] = Df;
Df.__name__ =
    "haxe.xml.Printer";
Df.print = function(a, b) {
    null == b && (b = !1);
    b = new Df(b);
    b.writeNode(a, "");
    return b.output.b
};
Df.prototype = {
    writeNode: function(a, b) {
        switch (a.nodeType) {
            case 0:
                this.output.b += H.string(b + "<");
                if (a.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                this.output.b += H.string(a.nodeName);
                for (var c = a.attributes(); c.hasNext();) {
                    var d = c.next();
                    this.output.b += H.string(" " + d + '="');
                    d = O.htmlEscape(a.get(d), !0);
                    this.output.b +=
                        H.string(d);
                    this.output.b += '"'
                }
                if (this.hasChildren(a)) {
                    this.output.b += ">";
                    this.pretty && (this.output.b += "\n");
                    if (a.nodeType != W.Document && a.nodeType != W.Element) throw X.thrown("Bad node type, expected Element or Document but found " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                    c = 0;
                    for (d = a.children; c < d.length;) {
                        var f = d[c++];
                        this.writeNode(f, this.pretty ? b + "\t" : b)
                    }
                    this.output.b += H.string(b + "</");
                    if (a.nodeType != W.Element) throw X.thrown("Bad node type, expected Element but found " + (null == a.nodeType ?
                        "null" : Cb.toString(a.nodeType)));
                    this.output.b += H.string(a.nodeName);
                    this.output.b += ">"
                } else this.output.b += "/>";
                this.pretty && (this.output.b += "\n");
                break;
            case 1:
                if (a.nodeType == W.Document || a.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                a = a.nodeValue;
                0 != a.length && (d = b + O.htmlEscape(a), this.output.b += H.string(d), this.pretty && (this.output.b += "\n"));
                break;
            case 2:
                this.output.b += H.string(b + "<![CDATA[");
                if (a.nodeType == W.Document || a.nodeType ==
                    W.Element) throw X.thrown("Bad node type, unexpected " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                this.output.b += H.string(a.nodeValue);
                this.output.b += "]]\x3e";
                this.pretty && (this.output.b += "\n");
                break;
            case 3:
                if (a.nodeType == W.Document || a.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                a = a.nodeValue;
                a = a.replace(/[\n\r\t]+/g, "");
                this.output.b += null == b ? "null" : "" + b;
                d = O.trim("\x3c!--" + a + "--\x3e");
                this.output.b += H.string(d);
                this.pretty &&
                    (this.output.b += "\n");
                break;
            case 4:
                if (a.nodeType == W.Document || a.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                this.output.b += H.string("<!DOCTYPE " + a.nodeValue + ">");
                this.pretty && (this.output.b += "\n");
                break;
            case 5:
                if (a.nodeType == W.Document || a.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                this.output.b += H.string("<?" + a.nodeValue + "?>");
                this.pretty && (this.output.b +=
                    "\n");
                break;
            case 6:
                if (a.nodeType != W.Document && a.nodeType != W.Element) throw X.thrown("Bad node type, expected Element or Document but found " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
                c = 0;
                for (d = a.children; c < d.length;) f = d[c++], this.writeNode(f, b)
        }
    },
    hasChildren: function(a) {
        if (a.nodeType != W.Document && a.nodeType != W.Element) throw X.thrown("Bad node type, expected Element or Document but found " + (null == a.nodeType ? "null" : Cb.toString(a.nodeType)));
        var b = 0;
        for (a = a.children; b < a.length;) {
            var c = a[b++];
            switch (c.nodeType) {
                case 0:
                case 1:
                    return !0;
                case 2:
                case 3:
                    if (c.nodeType == W.Document || c.nodeType == W.Element) throw X.thrown("Bad node type, unexpected " + (null == c.nodeType ? "null" : Cb.toString(c.nodeType)));
                    if (0 != O.ltrim(c.nodeValue).length) return !0
            }
        }
        return !1
    },
    __class__: Df
};
var Cg = y["haxe.zip.ExtraField"] = {
    __ename__: "haxe.zip.ExtraField",
    __constructs__: null,
    FUnknown: (G = function(a, b) {
            return {
                _hx_index: 0,
                tag: a,
                bytes: b,
                __enum__: "haxe.zip.ExtraField",
                toString: r
            }
        }, G._hx_name = "FUnknown", G.__params__ = ["tag", "bytes"],
        G),
    FInfoZipUnicodePath: (G = function(a, b) {
        return {
            _hx_index: 1,
            name: a,
            crc: b,
            __enum__: "haxe.zip.ExtraField",
            toString: r
        }
    }, G._hx_name = "FInfoZipUnicodePath", G.__params__ = ["name", "crc"], G),
    FUtf8: {
        _hx_name: "FUtf8",
        _hx_index: 2,
        __enum__: "haxe.zip.ExtraField",
        toString: r
    }
};
Cg.__constructs__ = [Cg.FUnknown, Cg.FInfoZipUnicodePath, Cg.FUtf8];
var pe = y["haxe.zip.Huffman"] = {
    __ename__: "haxe.zip.Huffman",
    __constructs__: null,
    Found: (G = function(a) {
            return {
                _hx_index: 0,
                i: a,
                __enum__: "haxe.zip.Huffman",
                toString: r
            }
        }, G._hx_name = "Found",
        G.__params__ = ["i"], G),
    NeedBit: (G = function(a, b) {
        return {
            _hx_index: 1,
            left: a,
            right: b,
            __enum__: "haxe.zip.Huffman",
            toString: r
        }
    }, G._hx_name = "NeedBit", G.__params__ = ["left", "right"], G),
    NeedBits: (G = function(a, b) {
        return {
            _hx_index: 2,
            n: a,
            table: b,
            __enum__: "haxe.zip.Huffman",
            toString: r
        }
    }, G._hx_name = "NeedBits", G.__params__ = ["n", "table"], G)
};
pe.__constructs__ = [pe.Found, pe.NeedBit, pe.NeedBits];
var mk = function() {};
g["haxe.zip.HuffTools"] = mk;
mk.__name__ = "haxe.zip.HuffTools";
mk.prototype = {
    treeDepth: function(a) {
        switch (a._hx_index) {
            case 0:
                return 0;
            case 1:
                var b = a.right;
                a = this.treeDepth(a.left);
                b = this.treeDepth(b);
                return 1 + (a < b ? a : b);
            case 2:
                throw X.thrown("assert");
        }
    },
    treeCompress: function(a) {
        var b = this.treeDepth(a);
        if (0 == b) return a;
        if (1 == b) {
            if (1 == a._hx_index) return b = a.right, pe.NeedBit(this.treeCompress(a.left), this.treeCompress(b));
            throw X.thrown("assert");
        }
        for (var c = [], d = 0, f = 1 << b; d < f;) d++, c.push(pe.Found(-1));
        this.treeWalk(c, 0, 0, b, a);
        return pe.NeedBits(b, c)
    },
    treeWalk: function(a, b, c, d, f) {
        if (1 == f._hx_index) {
            var h = f.left,
                k = f.right;
            0 < d ? (this.treeWalk(a,
                b, c + 1, d - 1, h), this.treeWalk(a, b | 1 << c, c + 1, d - 1, k)) : a[b] = this.treeCompress(f)
        } else a[b] = this.treeCompress(f)
    },
    treeMake: function(a, b, c, d) {
        if (d > b) throw X.thrown("Invalid huffman");
        var f = c << 5 | d;
        if (a.h.hasOwnProperty(f)) return pe.Found(a.h[f]);
        c <<= 1;
        ++d;
        return pe.NeedBit(this.treeMake(a, b, c, d), this.treeMake(a, b, c | 1, d))
    },
    make: function(a, b, c, d) {
        if (1 == c) return pe.NeedBit(pe.Found(0), pe.Found(0));
        var f = [],
            h = [];
        if (32 < d) throw X.thrown("Invalid huffman");
        for (var k = 0, n = d; k < n;) k++, f.push(0), h.push(0);
        k = 0;
        for (n = c; k <
            n;) {
            var p = k++;
            p = a[p + b];
            if (p >= d) throw X.thrown("Invalid huffman");
            f[p]++
        }
        var g = 0;
        k = 1;
        for (n = d - 1; k < n;) p = k++, g = g + f[p] << 1, h[p] = g;
        f = new mc;
        k = 0;
        for (n = c; k < n;) p = k++, c = a[p + b], 0 != c && (g = h[c - 1], h[c - 1] = g + 1, f.h[g << 5 | c] = p);
        return this.treeCompress(pe.NeedBit(this.treeMake(f, d, 0, 1), this.treeMake(f, d, 1, 1)))
    },
    __class__: mk
};
var nk = function(a) {
    this.buffer = new zb(new ArrayBuffer(65536));
    this.pos = 0;
    a && (this.crc = new Ag)
};
g["haxe.zip._InflateImpl.Window"] = nk;
nk.__name__ = "haxe.zip._InflateImpl.Window";
nk.prototype = {
    slide: function() {
        null !=
            this.crc && this.crc.update(this.buffer, 0, 32768);
        var a = new zb(new ArrayBuffer(65536));
        this.pos -= 32768;
        a.blit(0, this.buffer, 32768, this.pos);
        this.buffer = a
    },
    addBytes: function(a, b, c) {
        65536 < this.pos + c && this.slide();
        this.buffer.blit(this.pos, a, b, c);
        this.pos += c
    },
    addByte: function(a) {
        65536 == this.pos && this.slide();
        this.buffer.b[this.pos] = a & 255;
        this.pos++
    },
    getLastChar: function() {
        return this.buffer.b[this.pos - 1]
    },
    available: function() {
        return this.pos
    },
    checksum: function() {
        null != this.crc && this.crc.update(this.buffer,
            0, this.pos);
        return this.crc
    },
    __class__: nk
};
var wc = y["haxe.zip._InflateImpl.State"] = {
    __ename__: "haxe.zip._InflateImpl.State",
    __constructs__: null,
    Head: {
        _hx_name: "Head",
        _hx_index: 0,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    },
    Block: {
        _hx_name: "Block",
        _hx_index: 1,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    },
    CData: {
        _hx_name: "CData",
        _hx_index: 2,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    },
    Flat: {
        _hx_name: "Flat",
        _hx_index: 3,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    },
    Crc: {
        _hx_name: "Crc",
        _hx_index: 4,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    },
    Dist: {
        _hx_name: "Dist",
        _hx_index: 5,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    },
    DistOne: {
        _hx_name: "DistOne",
        _hx_index: 6,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    },
    Done: {
        _hx_name: "Done",
        _hx_index: 7,
        __enum__: "haxe.zip._InflateImpl.State",
        toString: r
    }
};
wc.__constructs__ = [wc.Head, wc.Block, wc.CData, wc.Flat, wc.Crc, wc.Dist, wc.DistOne, wc.Done];
var ad = function(a, b, c) {
    null == c && (c = !0);
    null == b && (b = !0);
    this.isFinal = !1;
    this.htools = new mk;
    this.huffman = this.buildFixedHuffman();
    this.huffdist = null;
    this.dist = this.len = 0;
    this.state = b ? wc.Head : wc.Block;
    this.input = a;
    this.needed = this.nbits = this.bits = 0;
    this.output = null;
    this.outpos = 0;
    this.lengths = [];
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.lengths.push(-1);
    this.window = new nk(c)
};
g["haxe.zip.InflateImpl"] = ad;
ad.__name__ = "haxe.zip.InflateImpl";
ad.prototype = {
    buildFixedHuffman: function() {
        if (null != ad.FIXED_HUFFMAN) return ad.FIXED_HUFFMAN;
        for (var a = [], b = 0; 288 > b;) {
            var c = b++;
            a.push(143 >= c ? 8 : 255 >= c ? 9 : 279 >= c ? 7 : 8)
        }
        ad.FIXED_HUFFMAN = this.htools.make(a, 0, 288, 10);
        return ad.FIXED_HUFFMAN
    },
    readBytes: function(a, b, c) {
        this.needed = c;
        this.outpos = b;
        this.output = a;
        if (0 < c)
            for (; this.inflateLoop(););
        return c - this.needed
    },
    getBits: function(a) {
        for (; this.nbits < a;) this.bits |= this.input.readByte() << this.nbits, this.nbits += 8;
        var b = this.bits & (1 << a) - 1;
        this.nbits -= a;
        this.bits >>= a;
        return b
    },
    getBit: function() {
        0 == this.nbits && (this.nbits = 8, this.bits = this.input.readByte());
        var a = 1 == (this.bits & 1);
        this.nbits--;
        this.bits >>= 1;
        return a
    },
    getRevBits: function(a) {
        return 0 == a ? 0 : this.getBit() ? 1 << a - 1 | this.getRevBits(a - 1) : this.getRevBits(a - 1)
    },
    resetBits: function() {
        this.nbits = this.bits = 0
    },
    addBytes: function(a, b, c) {
        this.window.addBytes(a, b, c);
        this.output.blit(this.outpos, a, b, c);
        this.needed -= c;
        this.outpos += c
    },
    addByte: function(a) {
        this.window.addByte(a);
        this.output.b[this.outpos] = a & 255;
        this.needed--;
        this.outpos++
    },
    addDistOne: function(a) {
        for (var b = this.window.getLastChar(), c = 0; c < a;) c++, this.addByte(b)
    },
    addDist: function(a, b) {
        this.addBytes(this.window.buffer, this.window.pos - a, b)
    },
    applyHuffman: function(a) {
        switch (a._hx_index) {
            case 0:
                var b = a.i;
                return b;
            case 1:
                return b = a.left, a = a.right, this.applyHuffman(this.getBit() ?
                    a : b);
            case 2:
                return b = a.n, this.applyHuffman(a.table[this.getBits(b)])
        }
    },
    inflateLengths: function(a, b) {
        for (var c = 0, d = 0; c < b;) {
            var f = this.applyHuffman(this.huffman);
            switch (f) {
                case 0:
                case 1:
                case 2:
                case 3:
                case 4:
                case 5:
                case 6:
                case 7:
                case 8:
                case 9:
                case 10:
                case 11:
                case 12:
                case 13:
                case 14:
                case 15:
                    d = f;
                    a[c] = f;
                    ++c;
                    break;
                case 16:
                    f = c + 3 + this.getBits(2);
                    if (f > b) throw X.thrown("Invalid data");
                    for (; c < f;) a[c] = d, ++c;
                    break;
                case 17:
                    c += 3 + this.getBits(3);
                    if (c > b) throw X.thrown("Invalid data");
                    break;
                case 18:
                    c += 11 + this.getBits(7);
                    if (c > b) throw X.thrown("Invalid data");
                    break;
                default:
                    throw X.thrown("Invalid data");
            }
        }
    },
    inflateLoop: function() {
        switch (this.state._hx_index) {
            case 0:
                var a = this.input.readByte();
                if (8 != (a & 15)) throw X.thrown("Invalid data");
                var b = this.input.readByte(),
                    c = 0 != (b & 32);
                if (0 != ((a << 8) + b) % 31) throw X.thrown("Invalid data");
                if (c) throw X.thrown("Unsupported dictionary");
                this.state = wc.Block;
                return !0;
            case 1:
                switch (this.isFinal = this.getBit(), this.getBits(2)) {
                    case 0:
                        this.len = this.input.readUInt16();
                        if (this.input.readUInt16() !=
                            65535 - this.len) throw X.thrown("Invalid data");
                        this.state = wc.Flat;
                        a = this.inflateLoop();
                        this.resetBits();
                        return a;
                    case 1:
                        return this.huffman = this.buildFixedHuffman(), this.huffdist = null, this.state = wc.CData, !0;
                    case 2:
                        a = this.getBits(5) + 257;
                        b = this.getBits(5) + 1;
                        var d = this.getBits(4) + 4;
                        c = 0;
                        for (var f = d; c < f;) {
                            var h = c++;
                            this.lengths[ad.CODE_LENGTHS_POS[h]] = this.getBits(3)
                        }
                        c = d;
                        for (f = 19; c < f;) h = c++, this.lengths[ad.CODE_LENGTHS_POS[h]] = 0;
                        this.huffman = this.htools.make(this.lengths, 0, 19, 8);
                        d = [];
                        c = 0;
                        for (f = a + b; c < f;) c++,
                            d.push(0);
                        this.inflateLengths(d, a + b);
                        this.huffdist = this.htools.make(d, a, b, 16);
                        this.huffman = this.htools.make(d, 0, a, 16);
                        this.state = wc.CData;
                        return !0;
                    default:
                        throw X.thrown("Invalid data");
                }
            case 2:
                b = this.applyHuffman(this.huffman);
                if (256 > b) return this.addByte(b), 0 < this.needed;
                if (256 == b) this.state = this.isFinal ? wc.Crc : wc.Block;
                else {
                    b -= 257;
                    a = ad.LEN_EXTRA_BITS_TBL[b];
                    if (-1 == a) throw X.thrown("Invalid data");
                    this.len = ad.LEN_BASE_VAL_TBL[b] + this.getBits(a);
                    b = null == this.huffdist ? this.getRevBits(5) : this.applyHuffman(this.huffdist);
                    a = ad.DIST_EXTRA_BITS_TBL[b];
                    if (-1 == a) throw X.thrown("Invalid data");
                    this.dist = ad.DIST_BASE_VAL_TBL[b] + this.getBits(a);
                    if (this.dist > this.window.available()) throw X.thrown("Invalid data");
                    this.state = 1 == this.dist ? wc.DistOne : wc.Dist
                }
                return !0;
            case 3:
                return a = this.len < this.needed ? this.len : this.needed, b = this.input.read(a), this.len -= a, this.addBytes(b, 0, a), 0 == this.len && (this.state = this.isFinal ? wc.Crc : wc.Block), 0 < this.needed;
            case 4:
                a = this.window.checksum();
                if (null == a) return this.state = wc.Done, !0;
                b = Ag.read(this.input);
                if (!a.equals(b)) throw X.thrown("Invalid CRC");
                this.state = wc.Done;
                return !0;
            case 5:
                for (; 0 < this.len && 0 < this.needed;) a = this.len < this.dist ? this.len : this.dist, a = this.needed < a ? this.needed : a, this.addDist(this.dist, a), this.len -= a;
                0 == this.len && (this.state = wc.CData);
                return 0 < this.needed;
            case 6:
                return a = this.len < this.needed ? this.len : this.needed, this.addDistOne(a), this.len -= a, 0 == this.len && (this.state = wc.CData), 0 < this.needed;
            case 7:
                return !1
        }
    },
    __class__: ad
};
var th = function(a) {
    this.i = a
};
g["haxe.zip.Reader"] = th;
th.__name__ =
    "haxe.zip.Reader";
th.readZip = function(a) {
    return (new th(a)).read()
};
th.prototype = {
    readZipDate: function() {
        var a = this.i.readUInt16(),
            b = a >> 11 & 31,
            c = a >> 5 & 63;
        a &= 31;
        var d = this.i.readUInt16();
        return new Date((d >> 9) + 1980, (d >> 5 & 15) - 1, d & 31, b, c, a << 1)
    },
    readExtraFields: function(a) {
        for (var b = new ab; 0 < a;) {
            if (4 > a) throw X.thrown("Invalid extra fields data");
            var c = this.i.readUInt16(),
                d = this.i.readUInt16();
            if (a < d) throw X.thrown("Invalid extra fields data");
            if (28789 == c) {
                var f = this.i.readByte();
                if (1 != f) {
                    var h = new Li;
                    h.addByte(f);
                    h.add(this.i.read(d - 1));
                    b.add(Cg.FUnknown(c, h.getBytes()))
                } else c = this.i.readInt32(), f = this.i.read(d - 5).toString(), b.add(Cg.FInfoZipUnicodePath(f, c))
            } else b.add(Cg.FUnknown(c, this.i.read(d)));
            a -= 4 + d
        }
        return b
    },
    readEntryHeader: function() {
        var a = this.i,
            b = a.readInt32();
        if (33639248 == b || 101010256 == b) return null;
        if (67324752 != b) throw X.thrown("Invalid Zip Data");
        a.readUInt16();
        b = a.readUInt16();
        var c = 0 != (b & 2048);
        if (0 != (b & 63473)) throw X.thrown("Unsupported flags " + b);
        var d = a.readUInt16(),
            f = 0 != d;
        if (f && 8 != d) throw X.thrown("Unsupported compression " +
            d);
        d = this.readZipDate();
        var h = a.readInt32(),
            k = a.readInt32(),
            n = a.readInt32(),
            p = a.readInt16(),
            g = a.readInt16();
        a = a.readString(p);
        g = this.readExtraFields(g);
        c && g.push(Cg.FUtf8);
        0 != (b & 8) && (h = null);
        return {
            fileName: a,
            fileSize: n,
            fileTime: d,
            compressed: f,
            dataSize: k,
            data: null,
            crc32: h,
            extraFields: g
        }
    },
    read: function() {
        for (var a = new ab, b = null;;) {
            var c = this.readEntryHeader();
            if (null == c) break;
            if (null == c.crc32) {
                if (c.compressed) {
                    null == b && (b = new zb(new ArrayBuffer(65536)));
                    for (var d = new Li, f = new ad(this.i, !1, !1);;) {
                        var h =
                            f.readBytes(b, 0, 65536);
                        d.addBytes(b, 0, h);
                        if (65536 > h) break
                    }
                    c.data = d.getBytes()
                } else c.data = this.i.read(c.dataSize);
                c.crc32 = this.i.readInt32();
                134695760 == c.crc32 && (c.crc32 = this.i.readInt32());
                c.dataSize = this.i.readInt32();
                c.fileSize = this.i.readInt32();
                c.dataSize = c.fileSize;
                c.compressed = !1
            } else c.data = this.i.read(c.dataSize);
            a.add(c)
        }
        return a
    },
    __class__: th
};
var qf = function() {};
g["js.Browser"] = qf;
qf.__name__ = "js.Browser";
qf.__properties__ = {
    get_supported: "get_supported"
};
qf.get_supported = function() {
    return "undefined" !=
        typeof window && "undefined" != typeof window.location ? "string" == typeof window.location.protocol : !1
};
qf.getLocalStorage = function() {
    try {
        var a = window.localStorage;
        a.getItem("");
        if (0 == a.length) {
            var b = "_hx_" + Math.random();
            a.setItem(b, b);
            a.removeItem(b)
        }
        return a
    } catch (c) {
        return Ta.lastError = c, null
    }
};
var dl = function() {};
g["js.html._CanvasElement.CanvasUtil"] = dl;
dl.__name__ = "js.html._CanvasElement.CanvasUtil";
dl.getContextWebGL = function(a, b) {
    var c = a.getContext("webgl", b);
    if (null != c) return c;
    c = a.getContext("experimental-webgl",
        b);
    return null != c ? c : null
};
var Rj = function() {
    this.connected = !0;
    this.buttons = [];
    this.axes = []
};
g["lime._internal.backend.html5.GameDeviceData"] = Rj;
Rj.__name__ = "lime._internal.backend.html5.GameDeviceData";
Rj.prototype = {
    __class__: Rj
};
var pk = function(a) {
    this.parent = a;
    this.id = -1;
    this.gain = 1;
    this.position = new ok
};
g["lime._internal.backend.html5.HTML5AudioSource"] = pk;
pk.__name__ = "lime._internal.backend.html5.HTML5AudioSource";
pk.prototype = {
    dispose: function() {},
    init: function() {},
    play: function() {
        if (!this.playing &&
            null != this.parent.buffer && null != this.parent.buffer.__srcHowl) {
            this.playing = !0;
            var a = this.getCurrentTime();
            this.completed = !1;
            var b = this.parent.buffer.__srcHowl._volume;
            this.parent.buffer.__srcHowl._volume = this.parent.get_gain();
            this.id = this.parent.buffer.__srcHowl.play();
            this.parent.buffer.__srcHowl._volume = b;
            this.setPosition(this.parent.get_position());
            this.parent.buffer.__srcHowl.on("end", l(this, this.howl_onEnd), this.id);
            this.setCurrentTime(a)
        }
    },
    stop: function() {
        this.playing = !1;
        null != this.parent.buffer &&
            null != this.parent.buffer.__srcHowl && (this.parent.buffer.__srcHowl.stop(this.id), this.parent.buffer.__srcHowl.off("end", l(this, this.howl_onEnd), this.id))
    },
    howl_onEnd: function() {
        this.playing = !1;
        0 < this.loops ? (this.loops--, this.stop(), this.play()) : (null != this.parent.buffer && null != this.parent.buffer.__srcHowl && (this.parent.buffer.__srcHowl.stop(this.id), this.parent.buffer.__srcHowl.off("end", l(this, this.howl_onEnd), this.id)), this.completed = !0, this.parent.onComplete.dispatch())
    },
    getCurrentTime: function() {
        if (-1 ==
            this.id) return 0;
        if (this.completed) return this.getLength();
        if (null != this.parent.buffer && null != this.parent.buffer.__srcHowl) {
            var a = (1E3 * this.parent.buffer.__srcHowl.seek(this.id) | 0) - this.parent.offset;
            return 0 > a ? 0 : a
        }
        return 0
    },
    setCurrentTime: function(a) {
        if (null != this.parent.buffer && null != this.parent.buffer.__srcHowl) {
            var b = (a + this.parent.offset) / 1E3;
            0 > b && (b = 0);
            this.parent.buffer.__srcHowl.seek(b, this.id)
        }
        return a
    },
    getGain: function() {
        return this.gain
    },
    setGain: function(a) {
        null != this.parent.buffer && null !=
            this.parent.buffer.__srcHowl && -1 != this.id && this.parent.buffer.__srcHowl.volume(a, this.id);
        return this.gain = a
    },
    getLength: function() {
        return 0 != this.length ? this.length : null != this.parent.buffer && null != this.parent.buffer.__srcHowl ? 1E3 * this.parent.buffer.__srcHowl.duration() | 0 : 0
    },
    setLength: function(a) {
        return this.length = a
    },
    setLoops: function(a) {
        return this.loops = a
    },
    getPosition: function() {
        return this.position
    },
    setPosition: function(a) {
        this.position.x = a.x;
        this.position.y = a.y;
        this.position.z = a.z;
        this.position.w =
            a.w;
        null != this.parent.buffer && null != this.parent.buffer.__srcHowl && null != this.parent.buffer.__srcHowl.pos && this.parent.buffer.__srcHowl.pos(this.position.x, this.position.y, this.position.z, this.id);
        return this.position
    },
    __class__: pk
};
var Ba = function() {
    this.validStatus0 = (new ja("Tizen", "gi")).match(window.navigator.userAgent)
};
g["lime._internal.backend.html5.HTML5HTTPRequest"] = Ba;
Ba.__name__ = "lime._internal.backend.html5.HTML5HTTPRequest";
Ba.loadImage = function(a) {
    var b = new Gd;
    Ba.activeRequests < Ba.requestLimit ?
        (Ba.activeRequests++, Ba.__loadImage(a, b, 0)) : Ba.requestQueue.add({
            instance: null,
            uri: a,
            promise: b,
            type: "IMAGE",
            options: 0
        });
    return b.future
};
Ba.loadImageFromBytes = function(a, b) {
    var c = URL.createObjectURL(new Blob([a.b.bufferValue], {
        type: b
    }));
    return null != c ? (a = new Gd, Ba.activeRequests < Ba.requestLimit ? (Ba.activeRequests++, Ba.__loadImage(c, a, 1)) : Ba.requestQueue.add({
        instance: null,
        uri: c,
        promise: a,
        type: "IMAGE",
        options: 1
    }), a.future) : Ba.loadImage("data:" + b + ";base64," + Be.encode(a))
};
Ba.processQueue = function() {
    if (Ba.activeRequests <
        Ba.requestLimit && 0 < Ba.requestQueue.length) {
        Ba.activeRequests++;
        var a = Ba.requestQueue.pop();
        switch (a.type) {
            case "BINARY":
                a.instance.__loadData(a.uri, a.promise);
                break;
            case "IMAGE":
                Ba.__loadImage(a.uri, a.promise, a.options);
                break;
            case "TEXT":
                a.instance.__loadText(a.uri, a.promise);
                break;
            default:
                Ba.activeRequests--
        }
    }
};
Ba.__fixHostname = function(a) {
    return null == a ? "" : a
};
Ba.__fixPort = function(a, b) {
    if (null == a || "" == a) switch (b) {
        case "ftp:":
            return "21";
        case "gopher:":
            return "70";
        case "http:":
            return "80";
        case "https:":
            return "443";
        case "ws:":
            return "80";
        case "wss:":
            return "443";
        default:
            return ""
    }
    return a
};
Ba.__fixProtocol = function(a) {
    return null == a || "" == a ? "http:" : a
};
Ba.__isInMemoryURI = function(a) {
    return O.startsWith(a, "data:") ? !0 : O.startsWith(a, "blob:")
};
Ba.__isSameOrigin = function(a) {
    if (null == a || "" == a || Ba.__isInMemoryURI(a)) return !0;
    null == Ba.originElement && (Ba.originElement = window.document.createElement("a"), Ba.originHostname = Ba.__fixHostname(E.location.hostname), Ba.originProtocol = Ba.__fixProtocol(E.location.protocol), Ba.originPort =
        Ba.__fixPort(E.location.port, Ba.originProtocol));
    var b = Ba.originElement;
    b.href = a;
    "" == b.hostname && (b.href = b.href);
    var c = Ba.__fixHostname(b.hostname);
    a = Ba.__fixProtocol(b.protocol);
    b = Ba.__fixPort(b.port, a);
    c = "" == c || c == Ba.originHostname;
    b = "" == b || b == Ba.originPort;
    return "file:" != a && c ? b : !1
};
Ba.__loadImage = function(a, b, c) {
    var d = new window.Image;
    Ba.__isSameOrigin(a) || (d.crossOrigin = "Anonymous");
    null == Ba.supportsImageProgress && (Ba.supportsImageProgress = "onprogress" in d);
    if (Ba.supportsImageProgress || Ba.__isInMemoryURI(a)) d.addEventListener("load",
        function(f) {
            Ba.__revokeBlobURI(a, c);
            f = new Ve(null, d.width, d.height);
            f.__srcImage = d;
            Ba.activeRequests--;
            Ba.processQueue();
            b.complete(new Xb(f))
        }, !1), d.addEventListener("progress", function(a) {
        b.progress(a.loaded, a.total)
    }, !1), d.addEventListener("error", function(d) {
        Ba.__revokeBlobURI(a, c);
        Ba.activeRequests--;
        Ba.processQueue();
        b.error(new Dg(d.detail, null))
    }, !1), d.src = a;
    else {
        var f = new XMLHttpRequest;
        f.onload = function(a) {
            Ba.activeRequests--;
            Ba.processQueue();
            (new Xb).__fromBytes(zb.ofData(f.response),
                function(a) {
                    b.complete(a)
                })
        };
        f.onerror = function(a) {
            b.error(new Dg(a.message, null))
        };
        f.onprogress = function(a) {
            a.lengthComputable && b.progress(a.loaded, a.total)
        };
        f.open("GET", a, !0);
        f.responseType = "arraybuffer";
        f.overrideMimeType("text/plain; charset=x-user-defined");
        f.send(null)
    }
};
Ba.__revokeBlobURI = function(a, b) {
    0 != (b & 1) && URL.revokeObjectURL(a)
};
Ba.prototype = {
    init: function(a) {
        this.parent = a
    },
    load: function(a, b, c) {
        this.request = new XMLHttpRequest;
        "POST" == this.parent.method ? this.request.upload.addEventListener("progress",
            b, !1) : this.request.addEventListener("progress", b, !1);
        this.request.onreadystatechange = c;
        b = "";
        if (null == this.parent.data) {
            c = Object.keys(this.parent.formData.h);
            for (var d = c.length, f = 0; f < d;) {
                var h = c[f++];
                0 < b.length && (b += "&");
                var k = this.parent.formData.h[h];
                if (-1 < h.indexOf("[]") && k instanceof Array) {
                    var n = [];
                    for (k = L(k); k.hasNext();) {
                        var p = k.next();
                        n.push(encodeURIComponent(p))
                    }
                    n = n.join("&amp;" + h + "=");
                    b += encodeURIComponent(h) + "=" + n
                } else n = encodeURIComponent(h) + "=", h = H.string(k), b += n + encodeURIComponent(h)
            }
            "GET" ==
            this.parent.method && "" != b && (a = -1 < a.indexOf("?") ? a + ("&" + b) : a + ("?" + b), b = "")
        }
        this.request.open(H.string(this.parent.method), a, !0);
        0 < this.parent.timeout && (this.request.timeout = this.parent.timeout);
        this.binary && (this.request.responseType = "arraybuffer");
        a = null;
        n = 0;
        for (c = this.parent.headers; n < c.length;) d = c[n], ++n, "Content-Type" == d.name ? a = d.value : this.request.setRequestHeader(d.name, d.value);
        null != this.parent.contentType && (a = this.parent.contentType);
        null == a && (null != this.parent.data ? a = "application/octet-stream" :
            "" != b && (a = "application/x-www-form-urlencoded"));
        null != a && this.request.setRequestHeader("Content-Type", a);
        this.parent.withCredentials && (this.request.withCredentials = !0);
        null != this.parent.data ? this.request.send(this.parent.data.b.bufferValue) : this.request.send(b)
    },
    loadData: function(a) {
        var b = new Gd;
        Ba.activeRequests < Ba.requestLimit ? (Ba.activeRequests++, this.__loadData(a, b)) : Ba.requestQueue.add({
            instance: this,
            uri: a,
            promise: b,
            type: "BINARY",
            options: 0
        });
        return b.future
    },
    loadText: function(a) {
        var b = new Gd;
        Ba.activeRequests < Ba.requestLimit ? (Ba.activeRequests++, this.__loadText(a, b)) : Ba.requestQueue.add({
            instance: this,
            uri: a,
            promise: b,
            type: "TEXT",
            options: 0
        });
        return b.future
    },
    processResponse: function() {
        if (this.parent.enableResponseHeaders) {
            this.parent.responseHeaders = [];
            for (var a, b, c = 0, d = this.request.getAllResponseHeaders().split("\n"); c < d.length;) b = d[c], ++c, a = O.trim(N.substr(b, 0, b.indexOf(":"))), b = O.trim(N.substr(b, b.indexOf(":") + 1, null)), "" != a && this.parent.responseHeaders.push(new Oi(a, b))
        }
        this.parent.responseStatus =
            this.request.status
    },
    __loadData: function(a, b) {
        var c = this;
        this.binary = !0;
        this.load(a, function(a) {
            b.progress(a.loaded, a.total)
        }, function(a) {
            4 == c.request.readyState && (a = null, "" == c.request.responseType ? null != c.request.responseText && (a = zb.ofString(c.request.responseText)) : null != c.request.response && (a = zb.ofData(c.request.response)), null != c.request.status && (200 <= c.request.status && 400 > c.request.status || c.validStatus0 && 0 == c.request.status) ? (c.processResponse(), b.complete(a)) : (c.processResponse(), b.error(new Dg(c.request.status,
                a))), c.request = null, Ba.activeRequests--, Ba.processQueue())
        })
    },
    __loadText: function(a, b) {
        var c = this;
        this.binary = !1;
        this.load(a, function(a) {
            b.progress(a.loaded, a.total)
        }, function(a) {
            4 == c.request.readyState && (null != c.request.status && (200 <= c.request.status && 400 > c.request.status || c.validStatus0 && 0 == c.request.status) ? (c.processResponse(), b.complete(c.request.responseText)) : (c.processResponse(), b.error(new Dg(c.request.status, c.request.responseText))), c.request = null, Ba.activeRequests--, Ba.processQueue())
        })
    },
    __class__: Ba
};
var Ha = function(a) {
    this.imeCompositionActive = !1;
    this.unusedTouchesPool = new ab;
    this.scale = 1;
    this.currentTouches = new mc;
    this.parent = a;
    this.cursor = cc.DEFAULT;
    this.cacheMouseY = this.cacheMouseX = 0;
    var b = a.__attributes;
    Object.prototype.hasOwnProperty.call(b, "context") || (b.context = {});
    this.renderType = b.context.type;
    Object.prototype.hasOwnProperty.call(b, "element") && (a.element = b.element);
    var c = a.element;
    Object.prototype.hasOwnProperty.call(b, "allowHighDPI") && b.allowHighDPI && "dom" != this.renderType &&
        (this.scale = window.devicePixelRatio);
    a.__scale = this.scale;
    this.setWidth = Object.prototype.hasOwnProperty.call(b, "width") ? b.width : 0;
    this.setHeight = Object.prototype.hasOwnProperty.call(b, "height") ? b.height : 0;
    a.__width = this.setWidth;
    a.__height = this.setHeight;
    a.id = Ha.windowID++;
    c instanceof HTMLCanvasElement ? this.canvas = c : "dom" == this.renderType ? this.div = window.document.createElement("div") : this.canvas = window.document.createElement("canvas");
    if (null != this.canvas) {
        var d = this.canvas.style;
        d.setProperty("-webkit-transform",
            "translateZ(0)", null);
        d.setProperty("transform", "translateZ(0)", null)
    } else null != this.div && (d = this.div.style, d.setProperty("-webkit-transform", "translate3D(0,0,0)", null), d.setProperty("transform", "translate3D(0,0,0)", null), d.position = "relative", d.overflow = "hidden", d.setProperty("-webkit-user-select", "none", null), d.setProperty("-moz-user-select", "none", null), d.setProperty("-ms-user-select", "none", null), d.setProperty("-o-user-select", "none", null));
    0 == a.__width && 0 == a.__height && (null != c ? (a.__width = c.clientWidth,
        a.__height = c.clientHeight) : (a.__width = window.innerWidth, a.__height = window.innerHeight), this.cacheElementWidth = a.__width, this.cacheElementHeight = a.__height, this.resizeElement = !0);
    null != this.canvas ? (this.canvas.width = Math.round(a.__width * this.scale), this.canvas.height = Math.round(a.__height * this.scale), this.canvas.style.width = a.__width + "px", this.canvas.style.height = a.__height + "px") : (this.div.style.width = a.__width + "px", this.div.style.height = a.__height + "px");
    if (Object.prototype.hasOwnProperty.call(b, "resizable") &&
        b.resizable || !Object.prototype.hasOwnProperty.call(b, "width") && 0 == this.setWidth && 0 == this.setHeight) a.__resizable = !0;
    this.updateSize();
    if (null != c) {
        null != this.canvas ? c != this.canvas && c.appendChild(this.canvas) : c.appendChild(this.div);
        b = "mousedown mouseenter mouseleave mousemove mouseup wheel".split(" ");
        for (d = 0; d < b.length;) {
            var f = b[d];
            ++d;
            c.addEventListener(f, l(this, this.handleMouseEvent), !0)
        }
        c.addEventListener("contextmenu", l(this, this.handleContextMenuEvent), !0);
        c.addEventListener("dragstart", l(this,
            this.handleDragEvent), !0);
        c.addEventListener("dragover", l(this, this.handleDragEvent), !0);
        c.addEventListener("drop", l(this, this.handleDragEvent), !0);
        c.addEventListener("touchstart", l(this, this.handleTouchEvent), !0);
        c.addEventListener("touchmove", l(this, this.handleTouchEvent), !0);
        c.addEventListener("touchend", l(this, this.handleTouchEvent), !0);
        c.addEventListener("touchcancel", l(this, this.handleTouchEvent), !0);
        c.addEventListener("gamepadconnected", l(this, this.handleGamepadEvent), !0);
        c.addEventListener("gamepaddisconnected",
            l(this, this.handleGamepadEvent), !0)
    }
    this.createContext();
    "webgl" == a.context.type && (this.canvas.addEventListener("webglcontextlost", l(this, this.handleContextEvent), !1), this.canvas.addEventListener("webglcontextrestored", l(this, this.handleContextEvent), !1))
};
g["lime._internal.backend.html5.HTML5Window"] = Ha;
Ha.__name__ = "lime._internal.backend.html5.HTML5Window";
Ha.prototype = {
    close: function() {
        var a = this.parent.element;
        if (null != a) {
            null != this.canvas ? (a != this.canvas && a.removeChild(this.canvas), this.canvas =
                null) : null != this.div && (a.removeChild(this.div), this.div = null);
            for (var b = "mousedown mouseenter mouseleave mousemove mouseup wheel".split(" "), c = 0; c < b.length;) {
                var d = b[c];
                ++c;
                a.removeEventListener(d, l(this, this.handleMouseEvent), !0)
            }
            a.removeEventListener("contextmenu", l(this, this.handleContextMenuEvent), !0);
            a.removeEventListener("dragstart", l(this, this.handleDragEvent), !0);
            a.removeEventListener("dragover", l(this, this.handleDragEvent), !0);
            a.removeEventListener("drop", l(this, this.handleDragEvent), !0);
            a.removeEventListener("touchstart", l(this, this.handleTouchEvent), !0);
            a.removeEventListener("touchmove", l(this, this.handleTouchEvent), !0);
            a.removeEventListener("touchend", l(this, this.handleTouchEvent), !0);
            a.removeEventListener("touchcancel", l(this, this.handleTouchEvent), !0);
            a.removeEventListener("gamepadconnected", l(this, this.handleGamepadEvent), !0);
            a.removeEventListener("gamepaddisconnected", l(this, this.handleGamepadEvent), !0)
        }
        this.parent.application.__removeWindow(this.parent)
    },
    createContext: function() {
        var a =
            new qk,
            b = this.parent.__attributes.context;
        a.window = this.parent;
        a.attributes = b;
        if (null != this.div) a.dom = this.div, a.type = "dom", a.version = "";
        else if (null != this.canvas) {
            var c = null,
                d = "canvas" == this.renderType,
                f = "opengl" == this.renderType || "opengles" == this.renderType || "webgl" == this.renderType,
                h = !Object.prototype.hasOwnProperty.call(b, "version") || "1" != b.version,
                k = !1;
            if (f || !d && (!Object.prototype.hasOwnProperty.call(b, "hardware") || b.hardware)) {
                d = Object.prototype.hasOwnProperty.call(b, "background") && null == b.background;
                f = Object.prototype.hasOwnProperty.call(b, "colorDepth") ? b.colorDepth : 16;
                var n = Object.prototype.hasOwnProperty.call(b, "antialiasing") && 0 < b.antialiasing,
                    p = Object.prototype.hasOwnProperty.call(b, "depth") ? b.depth : !0,
                    g = Object.prototype.hasOwnProperty.call(b, "stencil") && b.stencil;
                b = Object.prototype.hasOwnProperty.call(b, "preserveDrawingBuffer") && b.preserveDrawingBuffer;
                b = {
                    alpha: d || 16 < f,
                    antialias: n,
                    depth: p,
                    premultipliedAlpha: !0,
                    stencil: g,
                    preserveDrawingBuffer: b,
                    failIfMajorPerformanceCaveat: !1
                };
                d = ["webgl",
                    "experimental-webgl"
                ];
                h && d.unshift("webgl2");
                for (h = 0; h < d.length && (f = d[h], ++h, c = this.canvas.getContext(f, b), null != c && "webgl2" == f && (k = !0), null == c););
            }
            null == c ? (a.canvas2D = this.canvas.getContext("2d"), a.type = "canvas", a.version = "") : (a.webgl = Lc.fromWebGL2RenderContext(c), k && (a.webgl2 = c), null == Xe.context && (Xe.context = c, Xe.type = "webgl", Xe.version = k ? 2 : 1), a.type = "webgl", a.version = k ? "2" : "1")
        }
        this.parent.context = a
    },
    focusTextInput: function() {
        var a = this;
        this.__focusPending || (this.__focusPending = !0, Qf.delay(function() {
            a.__focusPending = !1;
            a.textInputEnabled && Ha.textInput.focus()
        }, 20))
    },
    getFrameRate: function() {
        return null == this.parent.application ? 0 : 0 > this.parent.application.__backend.framePeriod ? 60 : 1E3 == this.parent.application.__backend.framePeriod ? 0 : 1E3 / this.parent.application.__backend.framePeriod
    },
    handleContextEvent: function(a) {
        switch (a.type) {
            case "webglcontextlost":
                a.cancelable && a.preventDefault();
                this.parent.context = null;
                this.parent.onRenderContextLost.dispatch();
                break;
            case "webglcontextrestored":
                this.createContext(), this.parent.onRenderContextRestored.dispatch(this.parent.context)
        }
    },
    handleContextMenuEvent: function(a) {
        (this.parent.onMouseUp.canceled || this.parent.onMouseDown.canceled) && a.cancelable && a.preventDefault()
    },
    handleCutOrCopyEvent: function(a) {
        var b = zc.get_text();
        null == b && (b = "");
        a.clipboardData.setData("text/plain", b);
        a.cancelable && a.preventDefault()
    },
    handleDragEvent: function(a) {
        switch (a.type) {
            case "dragover":
                return a.preventDefault(), !1;
            case "dragstart":
                if ("img" == va.__cast(a.target, HTMLElement).nodeName.toLowerCase() && a.cancelable) return a.preventDefault(), !1;
                break;
            case "drop":
                if (null !=
                    a.dataTransfer && 0 < a.dataTransfer.files.length) return this.parent.onDropFile.dispatch(a.dataTransfer.files), a.preventDefault(), !1
        }
        return !0
    },
    handleFocusEvent: function(a) {
        this.textInputEnabled && (null == a.relatedTarget || this.isDescendent(a.relatedTarget)) && this.focusTextInput()
    },
    handleGamepadEvent: function(a) {
        switch (a.type) {
            case "gamepadconnected":
                nc.__connect(a.gamepad.index);
                "standard" == a.gamepad.mapping && qc.__connect(a.gamepad.index);
                break;
            case "gamepaddisconnected":
                nc.__disconnect(a.gamepad.index),
                    qc.__disconnect(a.gamepad.index)
        }
    },
    handleInputEvent: function(a) {
        this.imeCompositionActive || Ha.textInput.value == Ha.dummyCharacter || (a = O.replace(Ha.textInput.value, Ha.dummyCharacter, ""), 0 < a.length && this.parent.onTextInput.dispatch(a), Ha.textInput.value = Ha.dummyCharacter)
    },
    handleMouseEvent: function(a) {
        if ("wheel" != a.type) {
            if (null != this.parent.element)
                if (null != this.canvas) {
                    var b = this.canvas.getBoundingClientRect();
                    var c = this.parent.__width / b.width * (a.clientX - b.left);
                    b = this.parent.__height / b.height * (a.clientY -
                        b.top)
                } else null != this.div ? (b = this.div.getBoundingClientRect(), c = a.clientX - b.left, b = a.clientY - b.top) : (b = this.parent.element.getBoundingClientRect(), c = this.parent.__width / b.width * (a.clientX - b.left), b = this.parent.__height / b.height * (a.clientY - b.top));
            else c = a.clientX, b = a.clientY;
            switch (a.type) {
                case "mousedown":
                    a.currentTarget == this.parent.element && window.addEventListener("mouseup", l(this, this.handleMouseEvent));
                    this.parent.clickCount = a.detail;
                    this.parent.onMouseDown.dispatch(c, b, a.button);
                    this.parent.clickCount =
                        0;
                    this.parent.onMouseDown.canceled && a.cancelable && a.preventDefault();
                    break;
                case "mouseenter":
                    a.target == this.parent.element && (this.parent.onEnter.dispatch(), this.parent.onEnter.canceled && a.cancelable && a.preventDefault());
                    break;
                case "mouseleave":
                    a.target == this.parent.element && (this.parent.onLeave.dispatch(), this.parent.onLeave.canceled && a.cancelable && a.preventDefault());
                    break;
                case "mousemove":
                    if (c != this.cacheMouseX || b != this.cacheMouseY) this.parent.onMouseMove.dispatch(c, b), this.parent.onMouseMoveRelative.dispatch(c -
                        this.cacheMouseX, b - this.cacheMouseY), (this.parent.onMouseMove.canceled || this.parent.onMouseMoveRelative.canceled) && a.cancelable && a.preventDefault();
                    break;
                case "mouseup":
                    window.removeEventListener("mouseup", l(this, this.handleMouseEvent)), a.currentTarget == this.parent.element && a.stopPropagation(), this.parent.clickCount = a.detail, this.parent.onMouseUp.dispatch(c, b, a.button), this.parent.clickCount = 0, this.parent.onMouseUp.canceled && a.cancelable && a.preventDefault()
            }
            this.cacheMouseX = c;
            this.cacheMouseY = b
        } else {
            switch (a.deltaMode) {
                case 0:
                    c =
                        rf.PIXELS;
                    break;
                case 1:
                    c = rf.LINES;
                    break;
                case 2:
                    c = rf.PAGES;
                    break;
                default:
                    c = rf.UNKNOWN
            }
            this.parent.onMouseWheel.dispatch(a.deltaX, -a.deltaY, c);
            this.parent.onMouseWheel.canceled && a.cancelable && a.preventDefault()
        }
    },
    handlePasteEvent: function(a) {
        if (-1 < a.clipboardData.types.indexOf("text/plain")) {
            var b = a.clipboardData.getData("text/plain");
            zc.set_text(b);
            this.textInputEnabled && this.parent.onTextInput.dispatch(b);
            a.cancelable && a.preventDefault()
        }
    },
    handleResizeEvent: function(a) {
        this.primaryTouch = null;
        this.updateSize()
    },
    handleTouchEvent: function(a) {
        a.cancelable && a.preventDefault();
        var b = null;
        null != this.parent.element && (b = null != this.canvas ? this.canvas.getBoundingClientRect() : null != this.div ? this.div.getBoundingClientRect() : this.parent.element.getBoundingClientRect());
        var c = this.setWidth,
            d = this.setHeight;
        if (0 == c || 0 == d) null != b ? (c = b.width, d = b.height) : d = c = 1;
        for (var f, h, k, n, p, g = 0, q = a.changedTouches; g < q.length;) {
            var m = q[g];
            ++g;
            null != b ? (h = c / b.width * (m.clientX - b.left), k = d / b.height * (m.clientY - b.top)) : (h = m.clientX, k = m.clientY);
            if ("touchstart" == a.type) f = this.unusedTouchesPool.pop(), null == f ? f = new dc(h / c, k / d, m.identifier, 0, 0, m.force, this.parent.id) : (f.x = h / c, f.y = k / d, f.id = m.identifier, f.dx = 0, f.dy = 0, f.pressure = m.force, f.device = this.parent.id), this.currentTouches.h[m.identifier] = f, dc.onStart.dispatch(f), null == this.primaryTouch && (this.primaryTouch = f), f == this.primaryTouch && this.parent.onMouseDown.dispatch(h, k, 0);
            else if (f = this.currentTouches.h[m.identifier], null != f) switch (n = f.x, p = f.y, f.x = h / c, f.y = k / d, f.dx = f.x - n, f.dy = f.y - p, f.pressure =
                m.force, a.type) {
                case "touchcancel":
                    dc.onCancel.dispatch(f);
                    this.currentTouches.remove(m.identifier);
                    this.unusedTouchesPool.add(f);
                    f == this.primaryTouch && (this.primaryTouch = null);
                    break;
                case "touchend":
                    dc.onEnd.dispatch(f);
                    this.currentTouches.remove(m.identifier);
                    this.unusedTouchesPool.add(f);
                    f == this.primaryTouch && (this.parent.onMouseUp.dispatch(h, k, 0), this.primaryTouch = null);
                    break;
                case "touchmove":
                    dc.onMove.dispatch(f), f == this.primaryTouch && this.parent.onMouseMove.dispatch(h, k)
            }
        }
    },
    isDescendent: function(a) {
        if (a ==
            this.parent.element) return !0;
        for (; null != a;) {
            if (a.parentNode == this.parent.element) return !0;
            a = a.parentNode
        }
        return !1
    },
    setClipboard: function(a) {
        null == Ha.textArea && (Ha.textArea = window.document.createElement("textarea"), Ha.textArea.style.height = "0px", Ha.textArea.style.left = "-100px", Ha.textArea.style.opacity = "0", Ha.textArea.style.position = "fixed", Ha.textArea.style.top = "-100px", Ha.textArea.style.width = "0px", window.document.body.appendChild(Ha.textArea));
        Ha.textArea.value = a;
        Ha.textArea.focus();
        Ha.textArea.select();
        window.document.queryCommandEnabled("copy") && window.document.execCommand("copy");
        this.textInputEnabled && this.focusTextInput()
    },
    setCursor: function(a) {
        if (this.cursor != a) {
            if (null == a) this.parent.element.style.cursor = "none";
            else {
                switch (a._hx_index) {
                    case 0:
                        var b = "default";
                        break;
                    case 1:
                        b = "crosshair";
                        break;
                    case 3:
                        b = "move";
                        break;
                    case 4:
                        b = "pointer";
                        break;
                    case 5:
                        b = "nesw-resize";
                        break;
                    case 6:
                        b = "ns-resize";
                        break;
                    case 7:
                        b = "nwse-resize";
                        break;
                    case 8:
                        b = "ew-resize";
                        break;
                    case 9:
                        b = "text";
                        break;
                    case 10:
                        b = "wait";
                        break;
                    case 11:
                        b = "wait";
                        break;
                    default:
                        b = "auto"
                }
                this.parent.element.style.cursor = b
            }
            this.cursor = a
        }
        return this.cursor
    },
    setTextInputEnabled: function(a) {
        if (a) {
            if (null == Ha.textInput) {
                Ha.textInput = window.document.createElement("input");
                var b = 0 <= E.navigator.userAgent.indexOf("Android") ? "password" : "text";
                Ha.textInput.type = b;
                Ha.textInput.style.position = "absolute";
                Ha.textInput.style.opacity = "0";
                Ha.textInput.style.color = "transparent";
                Ha.textInput.value = Ha.dummyCharacter;
                Ha.textInput.autocapitalize = "off";
                Ha.textInput.autocorrect =
                    "off";
                Ha.textInput.autocomplete = "off";
                Ha.textInput.style.left = "0px";
                Ha.textInput.style.top = "50%";
                (new ja("(iPad|iPhone|iPod).*OS 8_", "gi")).match(window.navigator.userAgent) ? (Ha.textInput.style.fontSize = "0px", Ha.textInput.style.width = "0px", Ha.textInput.style.height = "0px") : (Ha.textInput.style.width = "1px", Ha.textInput.style.height = "1px");
                Ha.textInput.style.pointerEvents = "none";
                Ha.textInput.style.zIndex = "-10000000"
            }
            null == Ha.textInput.parentNode && this.parent.element.appendChild(Ha.textInput);
            this.textInputEnabled ||
                (Ha.textInput.addEventListener("input", l(this, this.handleInputEvent), !0), Ha.textInput.addEventListener("blur", l(this, this.handleFocusEvent), !0), Ha.textInput.addEventListener("cut", l(this, this.handleCutOrCopyEvent), !0), Ha.textInput.addEventListener("copy", l(this, this.handleCutOrCopyEvent), !0), Ha.textInput.addEventListener("paste", l(this, this.handlePasteEvent), !0), Ha.textInput.addEventListener("compositionstart", l(this, this.handleCompositionstartEvent), !0), Ha.textInput.addEventListener("compositionend",
                    l(this, this.handleCompositionendEvent), !0));
            Ha.textInput.focus();
            Ha.textInput.select()
        } else null != Ha.textInput && (Ha.textInput.blur(), Ha.textInput.removeEventListener("input", l(this, this.handleInputEvent), !0), Ha.textInput.removeEventListener("blur", l(this, this.handleFocusEvent), !0), Ha.textInput.removeEventListener("cut", l(this, this.handleCutOrCopyEvent), !0), Ha.textInput.removeEventListener("copy", l(this, this.handleCutOrCopyEvent), !0), Ha.textInput.removeEventListener("paste", l(this, this.handlePasteEvent),
            !0), Ha.textInput.removeEventListener("compositionstart", l(this, this.handleCompositionstartEvent), !0), Ha.textInput.removeEventListener("compositionend", l(this, this.handleCompositionendEvent), !0));
        return this.textInputEnabled = a
    },
    setTextInputRect: function(a) {
        return this.textInputRect = a
    },
    handleCompositionstartEvent: function(a) {
        this.imeCompositionActive = !0
    },
    handleCompositionendEvent: function(a) {
        this.imeCompositionActive = !1;
        this.handleInputEvent(a)
    },
    updateSize: function() {
        if (this.parent.__resizable) {
            if (null !=
                this.parent.element) {
                var a = this.parent.element.clientWidth;
                var b = this.parent.element.clientHeight
            } else a = window.innerWidth, b = window.innerHeight;
            if (a != this.cacheElementWidth || b != this.cacheElementHeight) {
                this.cacheElementWidth = a;
                this.cacheElementHeight = b;
                var c = this.resizeElement || 0 == this.setWidth && 0 == this.setHeight;
                if (null != this.parent.element && (null == this.div || null != this.div && c))
                    if (c) {
                        if (this.parent.__width != a || this.parent.__height != b) this.parent.__width = a, this.parent.__height = b, null != this.canvas ?
                            this.parent.element != this.canvas && (this.canvas.width = Math.round(a * this.scale), this.canvas.height = Math.round(b * this.scale), this.canvas.style.width = a + "px", this.canvas.style.height = b + "px") : (this.div.style.width = a + "px", this.div.style.height = b + "px"), this.parent.onResize.dispatch(a, b)
                    } else {
                        c = 0 != this.setWidth ? a / this.setWidth : 1;
                        var d = 0 != this.setHeight ? b / this.setHeight : 1,
                            f = a,
                            h = b,
                            k = 0,
                            n = 0;
                        c < d ? (h = Math.floor(this.setHeight * c), n = Math.floor((b - h) / 2)) : (f = Math.floor(this.setWidth * d), k = Math.floor((a - f) / 2));
                        null != this.canvas ?
                            this.parent.element != this.canvas && (this.canvas.style.width = f + "px", this.canvas.style.height = h + "px", this.canvas.style.marginLeft = k + "px", this.canvas.style.marginTop = n + "px") : (this.div.style.width = f + "px", this.div.style.height = h + "px", this.div.style.marginLeft = k + "px", this.div.style.marginTop = n + "px")
                    }
            }
        }
    },
    __class__: Ha
};
var al = function() {};
g["lime._internal.format.BMP"] = al;
al.__name__ = "lime._internal.format.BMP";
al.encode = function(a, b) {
    if (a.get_premultiplied() || 0 != a.get_format()) a = a.clone(), a.set_premultiplied(!1),
        a.set_format(0);
    null == b && (b = Sf.RGB);
    var c = 14,
        d = 40,
        f = a.width * a.height * 4;
    if (null != b) switch (b._hx_index) {
        case 0:
            f = (3 * a.width + 3 * a.width % 4) * a.height;
            break;
        case 1:
            d = 108;
            break;
        case 2:
            c = 0, f += a.width * a.height
    }
    var h = new zb(new ArrayBuffer(c + d + f)),
        k = 0;
    0 < c && (h.b[k++] = 66, h.b[k++] = 77, h.setInt32(k, h.length), k += 4, h.setUInt16(k, 0), k += 2, h.setUInt16(k, 0), k += 2, h.setInt32(k, c + d), k += 4);
    h.setInt32(k, d);
    k += 4;
    h.setInt32(k, a.width);
    k += 4;
    h.setInt32(k, b == Sf.ICO ? 2 * a.height : a.height);
    k += 4;
    h.setUInt16(k, 1);
    k += 2;
    h.setUInt16(k, b ==
        Sf.RGB ? 24 : 32);
    k += 2;
    h.setInt32(k, b == Sf.BITFIELD ? 3 : 0);
    k += 4;
    h.setInt32(k, f);
    k += 4;
    h.setInt32(k, 11824);
    k += 4;
    h.setInt32(k, 11824);
    k += 4;
    h.setInt32(k, 0);
    k += 4;
    h.setInt32(k, 0);
    k += 4;
    if (b == Sf.BITFIELD)
        for (h.setInt32(k, 16711680), k += 4, h.setInt32(k, 65280), k += 4, h.setInt32(k, 255), k += 4, h.setInt32(k, -16777216), k += 4, h.b[k++] = 32, h.b[k++] = 110, h.b[k++] = 105, h.b[k++] = 87, c = 0; 48 > c;) c++, h.b[k++] = 0;
    d = a.getPixels(new Ld(0, 0, a.width, a.height), 1);
    if (null != b) switch (b._hx_index) {
        case 0:
            c = 0;
            for (b = a.height; c < b;) {
                var n = c++;
                n = 4 * (a.height -
                    1 - n) * a.width;
                for (var p = 0, g = a.width; p < g;) {
                    p++;
                    n++;
                    var q = d.b[n++];
                    var m = d.b[n++];
                    var u = d.b[n++];
                    h.b[k++] = u & 255;
                    h.b[k++] = m & 255;
                    h.b[k++] = q & 255
                }
                f = 0;
                for (var l = 3 * a.width % 4; f < l;) f++, h.b[k++] = 0
            }
            break;
        case 1:
            c = 0;
            for (b = a.height; c < b;)
                for (n = c++, n = 4 * (a.height - 1 - n) * a.width, p = 0, g = a.width; p < g;) {
                    p++;
                    var r = d.b[n++];
                    q = d.b[n++];
                    m = d.b[n++];
                    u = d.b[n++];
                    h.b[k++] = u & 255;
                    h.b[k++] = m & 255;
                    h.b[k++] = q & 255;
                    h.b[k++] = r & 255
                }
            break;
        case 2:
            f = new zb(new ArrayBuffer(a.width * a.height));
            c = l = 0;
            for (b = a.height; c < b;)
                for (n = c++, n = 4 * (a.height - 1 - n) *
                    a.width, p = 0, g = a.width; p < g;) p++, r = d.b[n++], q = d.b[n++], m = d.b[n++], u = d.b[n++], h.b[k++] = u & 255, h.b[k++] = m & 255, h.b[k++] = q & 255, h.b[k++] = r & 255, f.b[l++] = 0;
            h.blit(k, f, 0, a.width * a.height)
    }
    return h
};
var Sf = y["lime._internal.format.BMPType"] = {
    __ename__: "lime._internal.format.BMPType",
    __constructs__: null,
    RGB: {
        _hx_name: "RGB",
        _hx_index: 0,
        __enum__: "lime._internal.format.BMPType",
        toString: r
    },
    BITFIELD: {
        _hx_name: "BITFIELD",
        _hx_index: 1,
        __enum__: "lime._internal.format.BMPType",
        toString: r
    },
    ICO: {
        _hx_name: "ICO",
        _hx_index: 2,
        __enum__: "lime._internal.format.BMPType",
        toString: r
    }
};
Sf.__constructs__ = [Sf.RGB, Sf.BITFIELD, Sf.ICO];
var Be = function() {};
g["lime._internal.format.Base64"] = Be;
Be.__name__ = "lime._internal.format.Base64";
Be.encode = function(a) {
    var b = [],
        c = Be.DICTIONARY,
        d = Be.EXTENDED_DICTIONARY,
        f = a.length,
        h = Math.floor(f / 3),
        k = 2 * h;
    b.length = 2 * Math.ceil(f / 3);
    for (var n = 0, p = 0, g; p < k;) g = a.b[n] << 16 | a.b[n + 1] << 8 | a.b[n + 2], b[p] = d[g >> 12 & 4095], b[p + 1] = d[g & 4095], n += 3, p += 2;
    switch (f - 3 * h) {
        case 1:
            g = a.b[n] << 16;
            b[p] = d[g >> 12 & 4095];
            b[p + 1] = "==";
            break;
        case 2:
            g = a.b[n] << 16 | a.b[n + 1] << 8, b[p] = d[g >> 12 & 4095], b[p + 1] = c[g >> 6 & 63] + "="
    }
    return b.join("")
};
var el = function() {};
g["lime._internal.format.Deflate"] = el;
el.__name__ = "lime._internal.format.Deflate";
el.decompress = function(a) {
    a = pako.inflateRaw(a.b.bufferValue);
    return zb.ofData(a)
};
var fl = function() {};
g["lime._internal.format.GZip"] = fl;
fl.__name__ = "lime._internal.format.GZip";
fl.decompress = function(a) {
    a = pako.ungzip(a.b.bufferValue);
    return zb.ofData(a)
};
var bl = function() {};
g["lime._internal.format.JPEG"] =
    bl;
bl.__name__ = "lime._internal.format.JPEG";
bl.encode = function(a, b) {
    if (a.get_premultiplied() || 0 != a.get_format()) a = a.clone(), a.set_premultiplied(!1), a.set_format(0);
    Ka.convertToCanvas(a, !1);
    if (null != a.buffer.__srcCanvas) {
        a = a.buffer.__srcCanvas.toDataURL("image/jpeg", b / 100);
        a = window.atob(a.split(";base64,")[1]);
        b = new zb(new ArrayBuffer(a.length));
        for (var c = 0, d = a.length; c < d;) {
            var f = c++;
            b.b[f] = N.cca(a, f) & 255
        }
        return b
    }
    return null
};
var gl = function() {};
g["lime._internal.format.LZMA"] = gl;
gl.__name__ = "lime._internal.format.LZMA";
gl.decompress = function(a) {
    var b = LZMA.decompress;
    a = a.b.bufferValue;
    a = null != a ? new Uint8Array(a, 0) : null;
    b = b(a);
    return "string" == typeof b ? zb.ofString(b) : zb.ofData(b)
};
var kk = function() {};
g["lime._internal.format.PNG"] = kk;
kk.__name__ = "lime._internal.format.PNG";
kk.encode = function(a) {
    if (a.get_premultiplied() || 0 != a.get_format()) a = a.clone(), a.set_premultiplied(!1), a.set_format(0);
    Ka.convertToCanvas(a, !1);
    if (null != a.buffer.__srcCanvas) {
        a = a.buffer.__srcCanvas.toDataURL("image/png");
        a = window.atob(a.split(";base64,")[1]);
        for (var b = new zb(new ArrayBuffer(a.length)), c = 0, d = a.length; c < d;) {
            var f = c++;
            b.b[f] = N.cca(a, f) & 255
        }
        return b
    }
    return null
};
var hl = function() {};
g["lime._internal.format.Zlib"] = hl;
hl.__name__ = "lime._internal.format.Zlib";
hl.decompress = function(a) {
    a = pako.inflate(a.b.bufferValue);
    return zb.ofData(a)
};
var bc = function() {};
g["lime._internal.graphics.ImageDataUtil"] = bc;
bc.__name__ = "lime._internal.graphics.ImageDataUtil";
bc.colorTransform = function(a, b, c) {
    var d = a.buffer.data;
    if (null != d) {
        var f = a.buffer.format,
            h =
            a.buffer.premultiplied;
        b = new De(a, b);
        var k = Qc.getAlphaTable(c),
            n = Qc.getRedTable(c),
            p = Qc.getGreenTable(c);
        c = Qc.getBlueTable(c);
        for (var g, q, m = 0, u = 0, l = b.height; u < l;) {
            g = u++;
            g = b.byteOffset + b.stride * g;
            for (var r = 0, x = b.width; r < x;) {
                q = r++;
                q = g + 4 * q;
                var D = f,
                    w = h;
                null == w && (w = !1);
                null == D && (D = 0);
                switch (D) {
                    case 0:
                        m = (d[q] & 255) << 24 | (d[q + 1] & 255) << 16 | (d[q + 2] & 255) << 8 | d[q + 3] & 255;
                        break;
                    case 1:
                        m = (d[q + 1] & 255) << 24 | (d[q + 2] & 255) << 16 | (d[q + 3] & 255) << 8 | d[q] & 255;
                        break;
                    case 2:
                        m = (d[q + 2] & 255) << 24 | (d[q + 1] & 255) << 16 | (d[q] & 255) << 8 | d[q + 3] &
                            255
                }
                w && 0 != (m & 255) && 255 != (m & 255) && (R.unmult = 255 / (m & 255), m = (R.__clamp[Math.round((m >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((m >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((m >>> 8 & 255) * R.unmult)] & 255) << 8 | m & 255);
                m = (n[m >>> 24 & 255] & 255) << 24 | (p[m >>> 16 & 255] & 255) << 16 | (c[m >>> 8 & 255] & 255) << 8 | k[m & 255] & 255;
                D = f;
                w = h;
                null == w && (w = !1);
                null == D && (D = 0);
                w && (0 == (m & 255) ? 0 != m && (m = 0) : 255 != (m & 255) && (R.a16 = R.__alpha16[m & 255], m = ((m >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((m >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((m >>> 8 & 255) * R.a16 >>
                    16 & 255) << 8 | m & 255));
                switch (D) {
                    case 0:
                        d[q] = m >>> 24 & 255;
                        d[q + 1] = m >>> 16 & 255;
                        d[q + 2] = m >>> 8 & 255;
                        d[q + 3] = m & 255;
                        break;
                    case 1:
                        d[q] = m & 255;
                        d[q + 1] = m >>> 24 & 255;
                        d[q + 2] = m >>> 16 & 255;
                        d[q + 3] = m >>> 8 & 255;
                        break;
                    case 2:
                        d[q] = m >>> 8 & 255, d[q + 1] = m >>> 16 & 255, d[q + 2] = m >>> 24 & 255, d[q + 3] = m & 255
                }
            }
        }
        a.dirty = !0;
        a.version++
    }
};
bc.copyChannel = function(a, b, c, d, f, h) {
    switch (h._hx_index) {
        case 0:
            var k = 0;
            break;
        case 1:
            k = 1;
            break;
        case 2:
            k = 2;
            break;
        case 3:
            k = 3
    }
    switch (f._hx_index) {
        case 0:
            var n = 0;
            break;
        case 1:
            n = 1;
            break;
        case 2:
            n = 2;
            break;
        case 3:
            n = 3
    }
    f = b.buffer.data;
    h = a.buffer.data;
    if (null != f && null != h) {
        c = new De(b, c);
        d = new De(a, new Ld(d.x, d.y, c.width, c.height));
        var p = b.buffer.format,
            g = a.buffer.format;
        b = b.buffer.premultiplied;
        for (var q = a.buffer.premultiplied, m, u, l = 0, r = 0, x = 0, D = 0, w = d.height; D < w;) {
            u = D++;
            m = c.byteOffset + c.stride * u;
            u = d.byteOffset + d.stride * u;
            for (var z = 0, J = d.width; z < J;) {
                z++;
                var C = p,
                    y = b;
                null == y && (y = !1);
                null == C && (C = 0);
                switch (C) {
                    case 0:
                        l = (f[m] & 255) << 24 | (f[m + 1] & 255) << 16 | (f[m + 2] & 255) << 8 | f[m + 3] & 255;
                        break;
                    case 1:
                        l = (f[m + 1] & 255) << 24 | (f[m + 2] & 255) << 16 | (f[m + 3] & 255) <<
                            8 | f[m] & 255;
                        break;
                    case 2:
                        l = (f[m + 2] & 255) << 24 | (f[m + 1] & 255) << 16 | (f[m] & 255) << 8 | f[m + 3] & 255
                }
                y && 0 != (l & 255) && 255 != (l & 255) && (R.unmult = 255 / (l & 255), l = (R.__clamp[Math.round((l >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((l >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((l >>> 8 & 255) * R.unmult)] & 255) << 8 | l & 255);
                C = g;
                y = q;
                null == y && (y = !1);
                null == C && (C = 0);
                switch (C) {
                    case 0:
                        r = (h[u] & 255) << 24 | (h[u + 1] & 255) << 16 | (h[u + 2] & 255) << 8 | h[u + 3] & 255;
                        break;
                    case 1:
                        r = (h[u + 1] & 255) << 24 | (h[u + 2] & 255) << 16 | (h[u + 3] & 255) << 8 | h[u] & 255;
                        break;
                    case 2:
                        r = (h[u + 2] & 255) << 24 | (h[u + 1] & 255) << 16 | (h[u] & 255) << 8 | h[u + 3] & 255
                }
                y && 0 != (r & 255) && 255 != (r & 255) && (R.unmult = 255 / (r & 255), r = (R.__clamp[Math.round((r >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((r >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((r >>> 8 & 255) * R.unmult)] & 255) << 8 | r & 255);
                switch (n) {
                    case 0:
                        x = l >>> 24 & 255;
                        break;
                    case 1:
                        x = l >>> 16 & 255;
                        break;
                    case 2:
                        x = l >>> 8 & 255;
                        break;
                    case 3:
                        x = l & 255
                }
                switch (k) {
                    case 0:
                        r = (x & 255) << 24 | (r >>> 16 & 255) << 16 | (r >>> 8 & 255) << 8 | r & 255;
                        break;
                    case 1:
                        r = (r >>> 24 & 255) << 24 | (x & 255) <<
                            16 | (r >>> 8 & 255) << 8 | r & 255;
                        break;
                    case 2:
                        r = (r >>> 24 & 255) << 24 | (r >>> 16 & 255) << 16 | (x & 255) << 8 | r & 255;
                        break;
                    case 3:
                        r = (r >>> 24 & 255) << 24 | (r >>> 16 & 255) << 16 | (r >>> 8 & 255) << 8 | x & 255
                }
                C = g;
                y = q;
                null == y && (y = !1);
                null == C && (C = 0);
                y && (0 == (r & 255) ? 0 != r && (r = 0) : 255 != (r & 255) && (R.a16 = R.__alpha16[r & 255], r = ((r >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((r >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((r >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | r & 255));
                switch (C) {
                    case 0:
                        h[u] = r >>> 24 & 255;
                        h[u + 1] = r >>> 16 & 255;
                        h[u + 2] = r >>> 8 & 255;
                        h[u + 3] = r & 255;
                        break;
                    case 1:
                        h[u] = r & 255;
                        h[u + 1] = r >>> 24 & 255;
                        h[u + 2] =
                            r >>> 16 & 255;
                        h[u + 3] = r >>> 8 & 255;
                        break;
                    case 2:
                        h[u] = r >>> 8 & 255, h[u + 1] = r >>> 16 & 255, h[u + 2] = r >>> 24 & 255, h[u + 3] = r & 255
                }
                m += 4;
                u += 4
            }
        }
        a.dirty = !0;
        a.version++
    }
};
bc.copyPixels = function(a, b, c, d, f, h, k) {
    null == k && (k = !1);
    if (a.width == b.width && a.height == b.height && c.width == b.width && c.height == b.height && 0 == c.x && 0 == c.y && 0 == d.x && 0 == d.y && null == f && null == h && 0 == k && a.get_format() == b.get_format()) a.buffer.data.set(b.buffer.data);
    else {
        var n = b.buffer.data,
            p = a.buffer.data;
        if (null == n || null == p) return;
        c = new De(b, c);
        var g = new Ld(d.x, d.y, c.width,
            c.height);
        g = new De(a, g);
        var q = b.buffer.format,
            m = a.buffer.format,
            u = 0,
            r = 0,
            l = b.buffer.premultiplied,
            x = a.buffer.premultiplied,
            D = b.buffer.bitsPerPixel / 8 | 0,
            w = a.buffer.bitsPerPixel / 8 | 0,
            z = null != f && f.get_transparent(),
            C = k || z && !a.get_transparent() || !k && !a.get_transparent() && b.get_transparent();
        if (z)
            if (w = f.buffer.data, b = f.buffer.format, k = 0, f = new De(f, new Ld(c.x + (null == h ? 0 : h.x), c.y + (null == h ? 0 : h.y), c.width, c.height)), g.clip(d.x | 0, d.y | 0, f.width, f.height), C)
                for (C = 0, D = g.height; C < D;)
                    for (z = C++, d = c.byteOffset + c.stride *
                        z, h = g.byteOffset + g.stride * z, z = f.byteOffset + f.stride * z, G = 0, F = g.width; G < F;) {
                        G++;
                        var J = q;
                        var y = l;
                        null == y && (y = !1);
                        null == J && (J = 0);
                        switch (J) {
                            case 0:
                                u = (n[d] & 255) << 24 | (n[d + 1] & 255) << 16 | (n[d + 2] & 255) << 8 | n[d + 3] & 255;
                                break;
                            case 1:
                                u = (n[d + 1] & 255) << 24 | (n[d + 2] & 255) << 16 | (n[d + 3] & 255) << 8 | n[d] & 255;
                                break;
                            case 2:
                                u = (n[d + 2] & 255) << 24 | (n[d + 1] & 255) << 16 | (n[d] & 255) << 8 | n[d + 3] & 255
                        }
                        y && 0 != (u & 255) && 255 != (u & 255) && (R.unmult = 255 / (u & 255), u = (R.__clamp[Math.round((u >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((u >>> 16 & 255) * R.unmult)] &
                            255) << 16 | (R.__clamp[Math.round((u >>> 8 & 255) * R.unmult)] & 255) << 8 | u & 255);
                        J = m;
                        y = x;
                        null == y && (y = !1);
                        null == J && (J = 0);
                        switch (J) {
                            case 0:
                                r = (p[h] & 255) << 24 | (p[h + 1] & 255) << 16 | (p[h + 2] & 255) << 8 | p[h + 3] & 255;
                                break;
                            case 1:
                                r = (p[h + 1] & 255) << 24 | (p[h + 2] & 255) << 16 | (p[h + 3] & 255) << 8 | p[h] & 255;
                                break;
                            case 2:
                                r = (p[h + 2] & 255) << 24 | (p[h + 1] & 255) << 16 | (p[h] & 255) << 8 | p[h + 3] & 255
                        }
                        y && 0 != (r & 255) && 255 != (r & 255) && (R.unmult = 255 / (r & 255), r = (R.__clamp[Math.round((r >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((r >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((r >>>
                            8 & 255) * R.unmult)] & 255) << 8 | r & 255);
                        J = b;
                        y = !1;
                        null == y && (y = !1);
                        null == J && (J = 0);
                        switch (J) {
                            case 0:
                                k = (w[z] & 255) << 24 | (w[z + 1] & 255) << 16 | (w[z + 2] & 255) << 8 | w[z + 3] & 255;
                                break;
                            case 1:
                                k = (w[z + 1] & 255) << 24 | (w[z + 2] & 255) << 16 | (w[z + 3] & 255) << 8 | w[z] & 255;
                                break;
                            case 2:
                                k = (w[z + 2] & 255) << 24 | (w[z + 1] & 255) << 16 | (w[z] & 255) << 8 | w[z + 3] & 255
                        }
                        y && 0 != (k & 255) && 255 != (k & 255) && (R.unmult = 255 / (k & 255), k = (R.__clamp[Math.round((k >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((k >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((k >>> 8 & 255) * R.unmult)] &
                            255) << 8 | k & 255);
                        y = (k & 255) / 255 * ((u & 255) / 255);
                        if (0 < y) {
                            var v = (r & 255) / 255;
                            var t = 1 - y;
                            J = y + v * t;
                            I = R.__clamp[Math.round(((u >>> 24 & 255) * y + (r >>> 24 & 255) * v * t) / J)];
                            r = (I & 255) << 24 | (r >>> 16 & 255) << 16 | (r >>> 8 & 255) << 8 | r & 255;
                            I = R.__clamp[Math.round(((u >>> 16 & 255) * y + (r >>> 16 & 255) * v * t) / J)];
                            r = (r >>> 24 & 255) << 24 | (I & 255) << 16 | (r >>> 8 & 255) << 8 | r & 255;
                            y = R.__clamp[Math.round(((u >>> 8 & 255) * y + (r >>> 8 & 255) * v * t) / J)];
                            r = (r >>> 24 & 255) << 24 | (r >>> 16 & 255) << 16 | (y & 255) << 8 | r & 255;
                            J = R.__clamp[Math.round(255 * J)];
                            r = (r >>> 24 & 255) << 24 | (r >>> 16 & 255) << 16 | (r >>> 8 & 255) <<
                                8 | J & 255;
                            J = m;
                            y = x;
                            null == y && (y = !1);
                            null == J && (J = 0);
                            y && (0 == (r & 255) ? 0 != r && (r = 0) : 255 != (r & 255) && (R.a16 = R.__alpha16[r & 255], r = ((r >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((r >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((r >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | r & 255));
                            switch (J) {
                                case 0:
                                    p[h] = r >>> 24 & 255;
                                    p[h + 1] = r >>> 16 & 255;
                                    p[h + 2] = r >>> 8 & 255;
                                    p[h + 3] = r & 255;
                                    break;
                                case 1:
                                    p[h] = r & 255;
                                    p[h + 1] = r >>> 24 & 255;
                                    p[h + 2] = r >>> 16 & 255;
                                    p[h + 3] = r >>> 8 & 255;
                                    break;
                                case 2:
                                    p[h] = r >>> 8 & 255, p[h + 1] = r >>> 16 & 255, p[h + 2] = r >>> 24 & 255, p[h + 3] = r & 255
                            }
                        }
                        d += 4;
                        h += 4;
                        z += 4
                    } else
                        for (C = 0, D = g.height; C < D;)
                            for (z =
                                C++, d = c.byteOffset + c.stride * z, h = g.byteOffset + g.stride * z, z = f.byteOffset + f.stride * z, G = 0, F = g.width; G < F;) {
                                G++;
                                J = q;
                                y = l;
                                null == y && (y = !1);
                                null == J && (J = 0);
                                switch (J) {
                                    case 0:
                                        u = (n[d] & 255) << 24 | (n[d + 1] & 255) << 16 | (n[d + 2] & 255) << 8 | n[d + 3] & 255;
                                        break;
                                    case 1:
                                        u = (n[d + 1] & 255) << 24 | (n[d + 2] & 255) << 16 | (n[d + 3] & 255) << 8 | n[d] & 255;
                                        break;
                                    case 2:
                                        u = (n[d + 2] & 255) << 24 | (n[d + 1] & 255) << 16 | (n[d] & 255) << 8 | n[d + 3] & 255
                                }
                                y && 0 != (u & 255) && 255 != (u & 255) && (R.unmult = 255 / (u & 255), u = (R.__clamp[Math.round((u >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((u >>>
                                    16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((u >>> 8 & 255) * R.unmult)] & 255) << 8 | u & 255);
                                J = b;
                                y = !1;
                                null == y && (y = !1);
                                null == J && (J = 0);
                                switch (J) {
                                    case 0:
                                        k = (w[z] & 255) << 24 | (w[z + 1] & 255) << 16 | (w[z + 2] & 255) << 8 | w[z + 3] & 255;
                                        break;
                                    case 1:
                                        k = (w[z + 1] & 255) << 24 | (w[z + 2] & 255) << 16 | (w[z + 3] & 255) << 8 | w[z] & 255;
                                        break;
                                    case 2:
                                        k = (w[z + 2] & 255) << 24 | (w[z + 1] & 255) << 16 | (w[z] & 255) << 8 | w[z + 3] & 255
                                }
                                y && 0 != (k & 255) && 255 != (k & 255) && (R.unmult = 255 / (k & 255), k = (R.__clamp[Math.round((k >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((k >>> 16 & 255) * R.unmult)] &
                                    255) << 16 | (R.__clamp[Math.round((k >>> 8 & 255) * R.unmult)] & 255) << 8 | k & 255);
                                I = Math.round((k & 255) / 255 * (u & 255));
                                u = (u >>> 24 & 255) << 24 | (u >>> 16 & 255) << 16 | (u >>> 8 & 255) << 8 | I & 255;
                                J = m;
                                y = x;
                                null == y && (y = !1);
                                null == J && (J = 0);
                                y && (0 == (u & 255) ? 0 != u && (u = 0) : 255 != (u & 255) && (R.a16 = R.__alpha16[u & 255], u = ((u >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((u >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((u >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | u & 255));
                                switch (J) {
                                    case 0:
                                        p[h] = u >>> 24 & 255;
                                        p[h + 1] = u >>> 16 & 255;
                                        p[h + 2] = u >>> 8 & 255;
                                        p[h + 3] = u & 255;
                                        break;
                                    case 1:
                                        p[h] = u & 255;
                                        p[h + 1] = u >>> 24 & 255;
                                        p[h + 2] =
                                            u >>> 16 & 255;
                                        p[h + 3] = u >>> 8 & 255;
                                        break;
                                    case 2:
                                        p[h] = u >>> 8 & 255, p[h + 1] = u >>> 16 & 255, p[h + 2] = u >>> 24 & 255, p[h + 3] = u & 255
                                }
                                d += 4;
                                h += 4;
                                z += 4
                            } else if (C)
                                for (C = 0, D = g.height; C < D;) {
                                    z = C++;
                                    d = c.byteOffset + c.stride * z;
                                    h = g.byteOffset + g.stride * z;
                                    for (var G = 0, F = g.width; G < F;) {
                                        G++;
                                        J = q;
                                        y = l;
                                        null == y && (y = !1);
                                        null == J && (J = 0);
                                        switch (J) {
                                            case 0:
                                                u = (n[d] & 255) << 24 | (n[d + 1] & 255) << 16 | (n[d + 2] & 255) << 8 | n[d + 3] & 255;
                                                break;
                                            case 1:
                                                u = (n[d + 1] & 255) << 24 | (n[d + 2] & 255) << 16 | (n[d + 3] & 255) << 8 | n[d] & 255;
                                                break;
                                            case 2:
                                                u = (n[d + 2] & 255) << 24 | (n[d + 1] & 255) << 16 | (n[d] & 255) << 8 | n[d +
                                                    3] & 255
                                        }
                                        y && 0 != (u & 255) && 255 != (u & 255) && (R.unmult = 255 / (u & 255), u = (R.__clamp[Math.round((u >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((u >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((u >>> 8 & 255) * R.unmult)] & 255) << 8 | u & 255);
                                        J = m;
                                        y = x;
                                        null == y && (y = !1);
                                        null == J && (J = 0);
                                        switch (J) {
                                            case 0:
                                                r = (p[h] & 255) << 24 | (p[h + 1] & 255) << 16 | (p[h + 2] & 255) << 8 | p[h + 3] & 255;
                                                break;
                                            case 1:
                                                r = (p[h + 1] & 255) << 24 | (p[h + 2] & 255) << 16 | (p[h + 3] & 255) << 8 | p[h] & 255;
                                                break;
                                            case 2:
                                                r = (p[h + 2] & 255) << 24 | (p[h + 1] & 255) << 16 | (p[h] & 255) << 8 | p[h + 3] & 255
                                        }
                                        y && 0 !=
                                            (r & 255) && 255 != (r & 255) && (R.unmult = 255 / (r & 255), r = (R.__clamp[Math.round((r >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((r >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((r >>> 8 & 255) * R.unmult)] & 255) << 8 | r & 255);
                                        y = (u & 255) / 255;
                                        v = (r & 255) / 255;
                                        t = 1 - y;
                                        J = y + v * t;
                                        if (0 == J) r = 0;
                                        else {
                                            var I = R.__clamp[Math.round(((u >>> 24 & 255) * y + (r >>> 24 & 255) * v * t) / J)];
                                            r = (I & 255) << 24 | (r >>> 16 & 255) << 16 | (r >>> 8 & 255) << 8 | r & 255;
                                            I = R.__clamp[Math.round(((u >>> 16 & 255) * y + (r >>> 16 & 255) * v * t) / J)];
                                            r = (r >>> 24 & 255) << 24 | (I & 255) << 16 | (r >>> 8 & 255) << 8 | r &
                                                255;
                                            y = R.__clamp[Math.round(((u >>> 8 & 255) * y + (r >>> 8 & 255) * v * t) / J)];
                                            r = (r >>> 24 & 255) << 24 | (r >>> 16 & 255) << 16 | (y & 255) << 8 | r & 255;
                                            J = R.__clamp[Math.round(255 * J)];
                                            r = (r >>> 24 & 255) << 24 | (r >>> 16 & 255) << 16 | (r >>> 8 & 255) << 8 | J & 255
                                        }
                                        J = m;
                                        y = x;
                                        null == y && (y = !1);
                                        null == J && (J = 0);
                                        y && (0 == (r & 255) ? 0 != r && (r = 0) : 255 != (r & 255) && (R.a16 = R.__alpha16[r & 255], r = ((r >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((r >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((r >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | r & 255));
                                        switch (J) {
                                            case 0:
                                                p[h] = r >>> 24 & 255;
                                                p[h + 1] = r >>> 16 & 255;
                                                p[h + 2] = r >>> 8 & 255;
                                                p[h + 3] = r & 255;
                                                break;
                                            case 1:
                                                p[h] = r & 255;
                                                p[h + 1] = r >>> 24 & 255;
                                                p[h + 2] = r >>> 16 & 255;
                                                p[h + 3] = r >>> 8 & 255;
                                                break;
                                            case 2:
                                                p[h] = r >>> 8 & 255, p[h + 1] = r >>> 16 & 255, p[h + 2] = r >>> 24 & 255, p[h + 3] = r & 255
                                        }
                                        d += 4;
                                        h += 4
                                    }
                                } else if (q == m && l == x && D == w)
                                    for (C = 0, D = g.height; C < D;) z = C++, d = c.byteOffset + c.stride * z, h = g.byteOffset + g.stride * z, p.set(n.subarray(d, d + g.width * w), h);
                                else
                                    for (C = 0, D = g.height; C < D;)
                                        for (z = C++, d = c.byteOffset + c.stride * z, h = g.byteOffset + g.stride * z, G = 0, F = g.width; G < F;) {
                                            G++;
                                            J = q;
                                            y = l;
                                            null == y && (y = !1);
                                            null == J && (J = 0);
                                            switch (J) {
                                                case 0:
                                                    u = (n[d] & 255) << 24 | (n[d + 1] & 255) <<
                                                        16 | (n[d + 2] & 255) << 8 | n[d + 3] & 255;
                                                    break;
                                                case 1:
                                                    u = (n[d + 1] & 255) << 24 | (n[d + 2] & 255) << 16 | (n[d + 3] & 255) << 8 | n[d] & 255;
                                                    break;
                                                case 2:
                                                    u = (n[d + 2] & 255) << 24 | (n[d + 1] & 255) << 16 | (n[d] & 255) << 8 | n[d + 3] & 255
                                            }
                                            y && 0 != (u & 255) && 255 != (u & 255) && (R.unmult = 255 / (u & 255), u = (R.__clamp[Math.round((u >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((u >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((u >>> 8 & 255) * R.unmult)] & 255) << 8 | u & 255);
                                            J = m;
                                            y = x;
                                            null == y && (y = !1);
                                            null == J && (J = 0);
                                            y && (0 == (u & 255) ? 0 != u && (u = 0) : 255 != (u & 255) && (R.a16 = R.__alpha16[u &
                                                255], u = ((u >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((u >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((u >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | u & 255));
                                            switch (J) {
                                                case 0:
                                                    p[h] = u >>> 24 & 255;
                                                    p[h + 1] = u >>> 16 & 255;
                                                    p[h + 2] = u >>> 8 & 255;
                                                    p[h + 3] = u & 255;
                                                    break;
                                                case 1:
                                                    p[h] = u & 255;
                                                    p[h + 1] = u >>> 24 & 255;
                                                    p[h + 2] = u >>> 16 & 255;
                                                    p[h + 3] = u >>> 8 & 255;
                                                    break;
                                                case 2:
                                                    p[h] = u >>> 8 & 255, p[h + 1] = u >>> 16 & 255, p[h + 2] = u >>> 24 & 255, p[h + 3] = u & 255
                                            }
                                            d += 4;
                                            h += 4
                                        }
    }
    a.dirty = !0;
    a.version++
};
bc.fillRect = function(a, b, c, d) {
    switch (d) {
        case 1:
            c = (c >>> 16 & 255) << 24 | (c >>> 8 & 255) << 16 | (c & 255) << 8 | c >>> 24 & 255;
            break;
        case 2:
            c =
                (c >>> 8 & 255) << 24 | (c >>> 16 & 255) << 16 | (c >>> 24 & 255) << 8 | c & 255
    }
    a.get_transparent() || (c = (c >>> 24 & 255) << 24 | (c >>> 16 & 255) << 16 | (c >>> 8 & 255) << 8 | 255);
    var f = a.buffer.data;
    if (null != f) {
        d = a.buffer.format;
        var h = a.buffer.premultiplied;
        h && (0 == (c & 255) ? 0 != c && (c = 0) : 255 != (c & 255) && (R.a16 = R.__alpha16[c & 255], c = ((c >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((c >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((c >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | c & 255));
        b = new De(a, b);
        for (var k, n = 0, p = b.height; n < p;) {
            h = n++;
            k = b.byteOffset + b.stride * h;
            for (var g = 0, q = b.width; g < q;) {
                h = g++;
                var m =
                    k + 4 * h,
                    u = d;
                h = !1;
                null == h && (h = !1);
                null == u && (u = 0);
                h && (0 == (c & 255) ? 0 != c && (c = 0) : 255 != (c & 255) && (R.a16 = R.__alpha16[c & 255], c = ((c >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((c >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((c >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | c & 255));
                switch (u) {
                    case 0:
                        f[m] = c >>> 24 & 255;
                        f[m + 1] = c >>> 16 & 255;
                        f[m + 2] = c >>> 8 & 255;
                        f[m + 3] = c & 255;
                        break;
                    case 1:
                        f[m] = c & 255;
                        f[m + 1] = c >>> 24 & 255;
                        f[m + 2] = c >>> 16 & 255;
                        f[m + 3] = c >>> 8 & 255;
                        break;
                    case 2:
                        f[m] = c >>> 8 & 255, f[m + 1] = c >>> 16 & 255, f[m + 2] = c >>> 24 & 255, f[m + 3] = c & 255
                }
            }
        }
        a.dirty = !0;
        a.version++
    }
};
bc.gaussianBlur = function(a,
    b, c, d, f, h, k, n, p) {
    null == k && (k = 1);
    null == h && (h = 4);
    null == f && (f = 4);
    (n = a.get_premultiplied()) && a.set_premultiplied(!1);
    Cd.blur(a, b, c, d, f, h, k);
    a.dirty = !0;
    a.version++;
    n && a.set_premultiplied(!0);
    return a
};
bc.getPixel = function(a, b, c, d) {
    var f = 0,
        h = a.buffer.data;
    b = 4 * (c + a.offsetY) * a.buffer.width + 4 * (b + a.offsetX);
    c = a.buffer.format;
    a = a.buffer.premultiplied;
    null == a && (a = !1);
    null == c && (c = 0);
    switch (c) {
        case 0:
            f = (h[b] & 255) << 24 | (h[b + 1] & 255) << 16 | (h[b + 2] & 255) << 8 | h[b + 3] & 255;
            break;
        case 1:
            f = (h[b + 1] & 255) << 24 | (h[b + 2] & 255) << 16 | (h[b +
                3] & 255) << 8 | h[b] & 255;
            break;
        case 2:
            f = (h[b + 2] & 255) << 24 | (h[b + 1] & 255) << 16 | (h[b] & 255) << 8 | h[b + 3] & 255
    }
    a && 0 != (f & 255) && 255 != (f & 255) && (R.unmult = 255 / (f & 255), f = (R.__clamp[Math.round((f >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((f >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((f >>> 8 & 255) * R.unmult)] & 255) << 8 | f & 255);
    f = (f >>> 24 & 255) << 24 | (f >>> 16 & 255) << 16 | (f >>> 8 & 255) << 8 | 0;
    switch (d) {
        case 1:
            return (f & 255) << 24 | (f >>> 24 & 255) << 16 | (f >>> 16 & 255) << 8 | f >>> 8 & 255;
        case 2:
            return (f >>> 8 & 255) << 24 | (f >>> 16 & 255) << 16 | (f >>> 24 &
                255) << 8 | f & 255;
        default:
            return f
    }
};
bc.getPixels = function(a, b, c) {
    if (null == a.buffer.data) return null;
    var d = new zb(new ArrayBuffer(4 * (b.width * b.height | 0))),
        f = a.buffer.data,
        h = a.buffer.format,
        k = a.buffer.premultiplied;
    a = new De(a, b);
    for (var n, p, g = b = p = 0, q = a.height; g < q;) {
        n = g++;
        n = a.byteOffset + a.stride * n;
        for (var m = 0, u = a.width; m < u;) {
            m++;
            var r = h,
                l = k;
            null == l && (l = !1);
            null == r && (r = 0);
            switch (r) {
                case 0:
                    p = (f[n] & 255) << 24 | (f[n + 1] & 255) << 16 | (f[n + 2] & 255) << 8 | f[n + 3] & 255;
                    break;
                case 1:
                    p = (f[n + 1] & 255) << 24 | (f[n + 2] & 255) << 16 | (f[n + 3] &
                        255) << 8 | f[n] & 255;
                    break;
                case 2:
                    p = (f[n + 2] & 255) << 24 | (f[n + 1] & 255) << 16 | (f[n] & 255) << 8 | f[n + 3] & 255
            }
            l && 0 != (p & 255) && 255 != (p & 255) && (R.unmult = 255 / (p & 255), p = (R.__clamp[Math.round((p >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((p >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((p >>> 8 & 255) * R.unmult)] & 255) << 8 | p & 255);
            switch (c) {
                case 1:
                    p = (p & 255) << 24 | (p >>> 24 & 255) << 16 | (p >>> 16 & 255) << 8 | p >>> 8 & 255;
                    break;
                case 2:
                    p = (p >>> 8 & 255) << 24 | (p >>> 16 & 255) << 16 | (p >>> 24 & 255) << 8 | p & 255
            }
            d.b[b++] = p >>> 24 & 255;
            d.b[b++] = p >>> 16 & 255;
            d.b[b++] =
                p >>> 8 & 255;
            d.b[b++] = p & 255;
            n += 4
        }
    }
    return d
};
bc.multiplyAlpha = function(a) {
    var b = a.buffer.data;
    if (null != b && a.buffer.transparent) {
        for (var c = a.buffer.format, d = 0, f = 0, h = b.length / 4 | 0; f < h;) {
            var k = f++,
                n = 4 * k,
                p = c,
                g = !1;
            null == g && (g = !1);
            null == p && (p = 0);
            switch (p) {
                case 0:
                    d = (b[n] & 255) << 24 | (b[n + 1] & 255) << 16 | (b[n + 2] & 255) << 8 | b[n + 3] & 255;
                    break;
                case 1:
                    d = (b[n + 1] & 255) << 24 | (b[n + 2] & 255) << 16 | (b[n + 3] & 255) << 8 | b[n] & 255;
                    break;
                case 2:
                    d = (b[n + 2] & 255) << 24 | (b[n + 1] & 255) << 16 | (b[n] & 255) << 8 | b[n + 3] & 255
            }
            g && 0 != (d & 255) && 255 != (d & 255) && (R.unmult = 255 /
                (d & 255), d = (R.__clamp[Math.round((d >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((d >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((d >>> 8 & 255) * R.unmult)] & 255) << 8 | d & 255);
            k *= 4;
            n = c;
            p = !0;
            null == p && (p = !1);
            null == n && (n = 0);
            p && (0 == (d & 255) ? 0 != d && (d = 0) : 255 != (d & 255) && (R.a16 = R.__alpha16[d & 255], d = ((d >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((d >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((d >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | d & 255));
            switch (n) {
                case 0:
                    b[k] = d >>> 24 & 255;
                    b[k + 1] = d >>> 16 & 255;
                    b[k + 2] = d >>> 8 & 255;
                    b[k + 3] = d & 255;
                    break;
                case 1:
                    b[k] = d & 255;
                    b[k + 1] = d >>> 24 & 255;
                    b[k + 2] = d >>> 16 & 255;
                    b[k + 3] = d >>> 8 & 255;
                    break;
                case 2:
                    b[k] = d >>> 8 & 255, b[k + 1] = d >>> 16 & 255, b[k + 2] = d >>> 24 & 255, b[k + 3] = d & 255
            }
        }
        a.buffer.premultiplied = !0;
        a.dirty = !0;
        a.version++
    }
};
bc.setFormat = function(a, b) {
    var c = a.buffer.data;
    if (null != c) {
        var d = c.length / 4 | 0;
        switch (a.get_format()) {
            case 0:
                var f = 0;
                var h = 1;
                var k = 2;
                var n = 3;
                break;
            case 1:
                f = 1;
                h = 2;
                k = 3;
                n = 0;
                break;
            case 2:
                f = 2, h = 1, k = 0, n = 3
        }
        switch (b) {
            case 0:
                var p = 0;
                var g = 1;
                var q = 2;
                var m = 3;
                break;
            case 1:
                p = 1;
                g = 2;
                q = 3;
                m = 0;
                break;
            case 2:
                p = 2, g = 1, q = 0, m = 3
        }
        for (var u = 0; u <
            d;) {
            var r = 4 * u++;
            var l = c[r + f];
            var x = c[r + h];
            var D = c[r + k];
            var w = c[r + n];
            c[r + p] = l;
            c[r + g] = x;
            c[r + q] = D;
            c[r + m] = w
        }
        a.buffer.format = b;
        a.dirty = !0;
        a.version++
    }
};
bc.setPixel = function(a, b, c, d, f) {
    switch (f) {
        case 1:
            d = (d >>> 16 & 255) << 24 | (d >>> 8 & 255) << 16 | (d & 255) << 8 | d >>> 24 & 255;
            break;
        case 2:
            d = (d >>> 8 & 255) << 24 | (d >>> 16 & 255) << 16 | (d >>> 24 & 255) << 8 | d & 255
    }
    var h = 0,
        k = a.buffer.data,
        n = 4 * (c + a.offsetY) * a.buffer.width + 4 * (b + a.offsetX);
    f = a.buffer.format;
    var p = a.buffer.premultiplied;
    null == p && (p = !1);
    null == f && (f = 0);
    switch (f) {
        case 0:
            h = (k[n] & 255) <<
                24 | (k[n + 1] & 255) << 16 | (k[n + 2] & 255) << 8 | k[n + 3] & 255;
            break;
        case 1:
            h = (k[n + 1] & 255) << 24 | (k[n + 2] & 255) << 16 | (k[n + 3] & 255) << 8 | k[n] & 255;
            break;
        case 2:
            h = (k[n + 2] & 255) << 24 | (k[n + 1] & 255) << 16 | (k[n] & 255) << 8 | k[n + 3] & 255
    }
    p && 0 != (h & 255) && 255 != (h & 255) && (R.unmult = 255 / (h & 255), h = (R.__clamp[Math.round((h >>> 24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((h >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((h >>> 8 & 255) * R.unmult)] & 255) << 8 | h & 255);
    d = (d >>> 24 & 255) << 24 | (d >>> 16 & 255) << 16 | (d >>> 8 & 255) << 8 | h & 255;
    k = a.buffer.data;
    n = 4 * (c + a.offsetY) *
        a.buffer.width + 4 * (b + a.offsetX);
    f = a.buffer.format;
    p = a.buffer.premultiplied;
    null == p && (p = !1);
    null == f && (f = 0);
    p && (0 == (d & 255) ? 0 != d && (d = 0) : 255 != (d & 255) && (R.a16 = R.__alpha16[d & 255], d = ((d >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((d >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((d >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | d & 255));
    switch (f) {
        case 0:
            k[n] = d >>> 24 & 255;
            k[n + 1] = d >>> 16 & 255;
            k[n + 2] = d >>> 8 & 255;
            k[n + 3] = d & 255;
            break;
        case 1:
            k[n] = d & 255;
            k[n + 1] = d >>> 24 & 255;
            k[n + 2] = d >>> 16 & 255;
            k[n + 3] = d >>> 8 & 255;
            break;
        case 2:
            k[n] = d >>> 8 & 255, k[n + 1] = d >>> 16 & 255, k[n + 2] = d >>> 24 & 255,
                k[n + 3] = d & 255
    }
    a.dirty = !0;
    a.version++
};
bc.unmultiplyAlpha = function(a) {
    var b = a.buffer.data;
    if (null != b) {
        for (var c = a.buffer.format, d = 0, f = 0, h = b.length / 4 | 0; f < h;) {
            var k = f++,
                n = 4 * k,
                p = c,
                g = !0;
            null == g && (g = !1);
            null == p && (p = 0);
            switch (p) {
                case 0:
                    d = (b[n] & 255) << 24 | (b[n + 1] & 255) << 16 | (b[n + 2] & 255) << 8 | b[n + 3] & 255;
                    break;
                case 1:
                    d = (b[n + 1] & 255) << 24 | (b[n + 2] & 255) << 16 | (b[n + 3] & 255) << 8 | b[n] & 255;
                    break;
                case 2:
                    d = (b[n + 2] & 255) << 24 | (b[n + 1] & 255) << 16 | (b[n] & 255) << 8 | b[n + 3] & 255
            }
            g && 0 != (d & 255) && 255 != (d & 255) && (R.unmult = 255 / (d & 255), d = (R.__clamp[Math.round((d >>>
                24 & 255) * R.unmult)] & 255) << 24 | (R.__clamp[Math.round((d >>> 16 & 255) * R.unmult)] & 255) << 16 | (R.__clamp[Math.round((d >>> 8 & 255) * R.unmult)] & 255) << 8 | d & 255);
            k *= 4;
            n = c;
            p = !1;
            null == p && (p = !1);
            null == n && (n = 0);
            p && (0 == (d & 255) ? 0 != d && (d = 0) : 255 != (d & 255) && (R.a16 = R.__alpha16[d & 255], d = ((d >>> 24 & 255) * R.a16 >> 16 & 255) << 24 | ((d >>> 16 & 255) * R.a16 >> 16 & 255) << 16 | ((d >>> 8 & 255) * R.a16 >> 16 & 255) << 8 | d & 255));
            switch (n) {
                case 0:
                    b[k] = d >>> 24 & 255;
                    b[k + 1] = d >>> 16 & 255;
                    b[k + 2] = d >>> 8 & 255;
                    b[k + 3] = d & 255;
                    break;
                case 1:
                    b[k] = d & 255;
                    b[k + 1] = d >>> 24 & 255;
                    b[k + 2] = d >>> 16 & 255;
                    b[k + 3] = d >>> 8 & 255;
                    break;
                case 2:
                    b[k] = d >>> 8 & 255, b[k + 1] = d >>> 16 & 255, b[k + 2] = d >>> 24 & 255, b[k + 3] = d & 255
            }
        }
        a.buffer.premultiplied = !1;
        a.dirty = !0;
        a.version++
    }
};
var De = function(a, b) {
    this.image = a;
    null == b ? this.rect = a.get_rect() : (0 > b.x && (b.x = 0), 0 > b.y && (b.y = 0), b.x + b.width > a.width && (b.width = a.width - b.x), b.y + b.height > a.height && (b.height = a.height - b.y), 0 > b.width && (b.width = 0), 0 > b.height && (b.height = 0), this.rect = b);
    this.stride = a.buffer.get_stride();
    this.__update()
};
g["lime._internal.graphics._ImageDataUtil.ImageDataView"] =
    De;
De.__name__ = "lime._internal.graphics._ImageDataUtil.ImageDataView";
De.prototype = {
    clip: function(a, b, c, d) {
        null == this.tempRect && (this.tempRect = new Ld);
        this.tempRect.setTo(a, b, c, d);
        this.rect.intersection(this.tempRect, this.rect);
        this.__update()
    },
    __update: function() {
        this.x = Math.ceil(this.rect.x);
        this.y = Math.ceil(this.rect.y);
        this.width = Math.floor(this.rect.width);
        this.height = Math.floor(this.rect.height);
        this.byteOffset = this.stride * (this.y + this.image.offsetY) + 4 * (this.x + this.image.offsetX)
    },
    __class__: De
};
var Cd = function() {};
g["lime._internal.graphics.StackBlur"] = Cd;
Cd.__name__ = "lime._internal.graphics.StackBlur";
Cd.blur = function(a, b, c, d, f, h, k) {
    a.copyPixels(b, c, d);
    Cd.__stackBlurCanvasRGBA(a, c.width | 0, c.height | 0, f, h, k)
};
Cd.__stackBlurCanvasRGBA = function(a, b, c, d, f, h) {
    d = Math.round(d) >> 1;
    f = Math.round(f) >> 1;
    if (null != Cd.MUL_TABLE && (d >= Cd.MUL_TABLE.length && (d = Cd.MUL_TABLE.length - 1), f >= Cd.MUL_TABLE.length && (f = Cd.MUL_TABLE.length - 1), !(0 > d || 0 > f))) {
        1 > h && (h = 1);
        3 < h && (h = 3);
        a = a.get_data();
        var k, n = d + d + 1;
        var p = f + f +
            1;
        var g = b - 1,
            q = c - 1,
            m = d + 1,
            u = f + 1,
            r = new Eg,
            l = r;
        var x = 1;
        for (var D = n; x < D;) x++, l = l.n = new Eg;
        l.n = r;
        var w = n = new Eg;
        x = 1;
        for (D = p; x < D;) x++, w = w.n = new Eg;
        w.n = n;
        for (var z = Cd.MUL_TABLE[d], J = Cd.SHG_TABLE[d], y = Cd.MUL_TABLE[f], C = Cd.SHG_TABLE[f]; 0 < h;) {
            --h;
            var v = k = 0;
            var t = z,
                G = J;
            var F = c;
            do {
                var I = a[k];
                p = m * I;
                var eb = a[k + 1];
                var O = m * eb;
                var H = a[k + 2];
                var lb = m * H;
                x = a[k + 3];
                var K = m * x;
                l = r;
                w = m;
                do l.r = I, l.g = eb, l.b = H, l.a = x, l = l.n; while (-1 < --w);
                x = 1;
                for (D = m; x < D;) w = x++, w = k + ((g < w ? g : w) << 2), p += l.r = a[w], O += l.g = a[w + 1], lb += l.b = a[w + 2], K += l.a = a[w +
                    3], l = l.n;
                D = r;
                l = 0;
                for (x = b; l < x;) w = l++, a[k++] = p * t >>> G, a[k++] = O * t >>> G, a[k++] = lb * t >>> G, a[k++] = K * t >>> G, w = w + d + 1, w = v + (w < g ? w : g) << 2, p -= D.r - (D.r = a[w]), O -= D.g - (D.g = a[w + 1]), lb -= D.b - (D.b = a[w + 2]), K -= D.a - (D.a = a[w + 3]), D = D.n;
                v += b
            } while (0 < --F);
            t = y;
            G = C;
            F = 0;
            for (v = b; F < v;) {
                l = F++;
                k = l << 2;
                I = a[k];
                p = u * I;
                eb = a[k + 1];
                O = u * eb;
                H = a[k + 2];
                lb = u * H;
                x = a[k + 3];
                K = u * x;
                w = n;
                k = 0;
                for (D = u; k < D;) k++, w.r = I, w.g = eb, w.b = H, w.a = x, w = w.n;
                x = b;
                D = 1;
                for (I = f + 1; D < I;) eb = D++, k = x + l << 2, p += w.r = a[k], O += w.g = a[k + 1], lb += w.b = a[k + 2], K += w.a = a[k + 3], w = w.n, eb < q && (x += b);
                k = l;
                D = n;
                if (0 < h)
                    for (I = 0, eb = c; I < eb;) H = I++, w = k << 2, x = K * t >>> G, a[w + 3] = x, 0 < x ? (a[w] = p * t >>> G, a[w + 1] = O * t >>> G, a[w + 2] = lb * t >>> G) : a[w] = a[w + 1] = a[w + 2] = 0, w = H + u, w = l + (w < q ? w : q) * b << 2, p -= D.r - (D.r = a[w]), O -= D.g - (D.g = a[w + 1]), lb -= D.b - (D.b = a[w + 2]), K -= D.a - (D.a = a[w + 3]), D = D.n, k += b;
                else
                    for (var Va = 0, E = c; Va < E;) {
                        var sa = Va++;
                        w = k << 2;
                        x = K * t >>> G;
                        a[w + 3] = x;
                        0 < x ? (x = 255 / x, I = (p * t >>> G) * x | 0, eb = (O * t >>> G) * x | 0, H = (lb * t >>> G) * x | 0, a[w] = 255 < I ? 255 : I, a[w + 1] = 255 < eb ? 255 : eb, a[w + 2] = 255 < H ? 255 : H) : a[w] = a[w + 1] = a[w + 2] = 0;
                        w = sa + u;
                        w = l + (w < q ? w : q) * b << 2;
                        p -= D.r - (D.r = a[w]);
                        O -= D.g -
                            (D.g = a[w + 1]);
                        lb -= D.b - (D.b = a[w + 2]);
                        K -= D.a - (D.a = a[w + 3]);
                        D = D.n;
                        k += b
                    }
            }
        }
    }
};
var Eg = function() {
    this.a = this.b = this.g = this.r = 0;
    this.n = null
};
g["lime._internal.graphics.BlurStack"] = Eg;
Eg.__name__ = "lime._internal.graphics.BlurStack";
Eg.prototype = {
    __class__: Eg
};
var hc = function(a, b) {
    if (null != a) try {
        this.value = a(), this.isComplete = !0
    } catch (c) {
        Ta.lastError = c, this.error = X.caught(c).unwrap(), this.isError = !0
    }
};
g["lime.app.Future"] = hc;
hc.__name__ = "lime.app.Future";
hc.withValue = function(a) {
    var b = new hc;
    b.isComplete = !0;
    b.value = a;
    return b
};
hc.prototype = {
    onComplete: function(a) {
        null != a && (this.isComplete ? a(this.value) : this.isError || (null == this.__completeListeners && (this.__completeListeners = []), this.__completeListeners.push(a)));
        return this
    },
    onError: function(a) {
        null != a && (this.isError ? a(this.error) : this.isComplete || (null == this.__errorListeners && (this.__errorListeners = []), this.__errorListeners.push(a)));
        return this
    },
    onProgress: function(a) {
        null != a && (null == this.__progressListeners && (this.__progressListeners = []), this.__progressListeners.push(a));
        return this
    },
    then: function(a) {
        if (this.isComplete) return a(this.value);
        if (this.isError) {
            var b = new hc;
            b.isError = !0;
            b.error = this.error;
            return b
        }
        var c = new Gd;
        this.onError(l(c, c.error));
        this.onProgress(l(c, c.progress));
        this.onComplete(function(b) {
            b = a(b);
            b.onError(l(c, c.error));
            b.onComplete(l(c, c.complete))
        });
        return c.future
    },
    __class__: hc
};
var Gd = function() {
    this.future = new hc
};
g["lime.app.Promise"] = Gd;
Gd.__name__ = "lime.app.Promise";
Gd.prototype = {
    complete: function(a) {
        if (!this.future.isError && (this.future.isComplete = !0, this.future.value = a, null != this.future.__completeListeners)) {
            for (var b = 0, c = this.future.__completeListeners; b < c.length;) {
                var d = c[b];
                ++b;
                d(a)
            }
            this.future.__completeListeners = null
        }
        return this
    },
    completeWith: function(a) {
        a.onComplete(l(this, this.complete));
        a.onError(l(this, this.error));
        a.onProgress(l(this, this.progress));
        return this
    },
    error: function(a) {
        if (!this.future.isComplete && (this.future.isError = !0, this.future.error = a, null != this.future.__errorListeners)) {
            for (var b = 0, c = this.future.__errorListeners; b <
                c.length;) {
                var d = c[b];
                ++b;
                d(a)
            }
            this.future.__errorListeners = null
        }
        return this
    },
    progress: function(a, b) {
        if (!this.future.isError && !this.future.isComplete && null != this.future.__progressListeners)
            for (var c = 0, d = this.future.__progressListeners; c < d.length;) {
                var f = d[c];
                ++c;
                f(a, b)
            }
        return this
    },
    __class__: Gd
};
var rk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Float_Float_Float_Void"] = rk;
rk.__name__ = "lime.app._Event_Float_Float_Float_Void";
rk.prototype = {
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b, c) {
        this.canceled = !1;
        for (var d = this.__listeners, f = this.__repeat, h = 0; h < d.length && (d[h](a, b, c), f[h] ? ++h : this.remove(d[h]), !this.canceled););
    },
    __class__: rk
};
var sk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Float_Float_Int_Void"] = sk;
sk.__name__ = "lime.app._Event_Float_Float_Int_Void";
sk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    cancel: function() {
        this.canceled = !0
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1),
            this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b, c) {
        this.canceled = !1;
        for (var d = this.__listeners, f = this.__repeat, h = 0; h < d.length && (d[h](a, b, c), f[h] ? ++h : this.remove(d[h]), !this.canceled););
    },
    __class__: sk
};
var uh = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Float_Float_Void"] = uh;
uh.__name__ = "lime.app._Event_Float_Float_Void";
uh.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h =
                d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b) {
        this.canceled = !1;
        for (var c = this.__listeners, d = this.__repeat, f = 0; f < c.length && (c[f](a, b), d[f] ? ++f : this.remove(c[f]),
                !this.canceled););
    },
    __class__: uh
};
var tk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Float_Float_lime_ui_MouseButton_Void"] = tk;
tk.__name__ = "lime.app._Event_Float_Float_lime_ui_MouseButton_Void";
tk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    cancel: function() {
        this.canceled = !0
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b, c) {
        this.canceled = !1;
        for (var d = this.__listeners, f = this.__repeat, h = 0; h < d.length && (d[h](a, b, c), f[h] ? ++h : this.remove(d[h]), !this.canceled););
    },
    __class__: tk
};
var uk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Float_Float_lime_ui_MouseWheelMode_Void"] = uk;
uk.__name__ = "lime.app._Event_Float_Float_lime_ui_MouseWheelMode_Void";
uk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    cancel: function() {
        this.canceled = !0
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b, c) {
        this.canceled = !1;
        for (var d = this.__listeners, f = this.__repeat, h = 0; h < d.length && (d[h](a, b, c), f[h] ? ++h : this.remove(d[h]), !this.canceled););
    },
    __class__: uk
};
var vk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Int_Float_Void"] = vk;
vk.__name__ = "lime.app._Event_Int_Float_Void";
vk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a,
        b) {
        this.canceled = !1;
        for (var c = this.__listeners, d = this.__repeat, f = 0; f < c.length && (c[f](a, b), d[f] ? ++f : this.remove(c[f]), !this.canceled););
    },
    __class__: vk
};
var Pi = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Int_Int_Void"] = Pi;
Pi.__name__ = "lime.app._Event_Int_Int_Void";
Pi.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b) {
        this.canceled = !1;
        for (var c = this.__listeners, d = this.__repeat, f = 0; f < c.length && (c[f](a, b), d[f] ? ++f : this.remove(c[f]), !this.canceled););
    },
    __class__: Pi
};
var zf = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Int_Void"] = zf;
zf.__name__ = "lime.app._Event_Int_Void";
zf.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <=
            --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: zf
};
var wk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Int_lime_ui_JoystickHatPosition_Void"] = wk;
wk.__name__ = "lime.app._Event_Int_lime_ui_JoystickHatPosition_Void";
wk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    __class__: wk
};
var xk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_String_Int_Int_Void"] = xk;
xk.__name__ = "lime.app._Event_String_Int_Int_Void";
xk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    __class__: xk
};
var Qi = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_String_Void"] = Qi;
Qi.__name__ = "lime.app._Event_String_Void";
Qi.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    cancel: function() {
        this.canceled = !0
    },
    has: function(a) {
        for (var b = 0, c = this.__listeners; b < c.length;) {
            var d = c[b];
            ++b;
            if (d == a) return !0
        }
        return !1
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <=
            --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: Qi
};
var Ac = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_Void_Void"] = Ac;
Ac.__name__ = "lime.app._Event_Void_Void";
Ac.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function() {
        this.canceled = !1;
        for (var a = this.__listeners,
                b = this.__repeat, c = 0; c < a.length && (a[c](), b[c] ? ++c : this.remove(a[c]), !this.canceled););
    },
    __class__: Ac
};
var Ri = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_graphics_RenderContext_Void"] = Ri;
Ri.__name__ = "lime.app._Event_lime_graphics_RenderContext_Void";
Ri.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h,
                    0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    cancel: function() {
        this.canceled = !0
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: Ri
};
var yk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_ui_GamepadAxis_Float_Void"] = yk;
yk.__name__ = "lime.app._Event_lime_ui_GamepadAxis_Float_Void";
yk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b =
                this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b) {
        this.canceled = !1;
        for (var c = this.__listeners, d = this.__repeat, f = 0; f < c.length && (c[f](a, b), d[f] ? ++f : this.remove(c[f]), !this.canceled););
    },
    __class__: yk
};
var Si = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_ui_GamepadButton_Void"] = Si;
Si.__name__ = "lime.app._Event_lime_ui_GamepadButton_Void";
Si.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: Si
};
var zk = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_ui_Gamepad_Void"] = zk;
zk.__name__ = "lime.app._Event_lime_ui_Gamepad_Void";
zk.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h,
                    0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: zk
};
var Ak = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_ui_Joystick_Void"] = Ak;
Ak.__name__ = "lime.app._Event_lime_ui_Joystick_Void";
Ak.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <=
            --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: Ak
};
var Ti = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_ui_KeyCode_lime_ui_KeyModifier_Void"] = Ti;
Ti.__name__ = "lime.app._Event_lime_ui_KeyCode_lime_ui_KeyModifier_Void";
Ti.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    cancel: function() {
        this.canceled = !0
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1),
            this.__repeat.splice(b, 1))
    },
    dispatch: function(a, b) {
        this.canceled = !1;
        for (var c = this.__listeners, d = this.__repeat, f = 0; f < c.length && (c[f](a, b), d[f] ? ++f : this.remove(c[f]), !this.canceled););
    },
    __class__: Ti
};
var Fg = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_ui_Touch_Void"] = Fg;
Fg.__name__ = "lime.app._Event_lime_ui_Touch_Void";
Fg.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h =
                d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]),
                !this.canceled););
    },
    __class__: Fg
};
var Sj = function() {
    this.canceled = !1;
    this.__listeners = [];
    this.__priorities = [];
    this.__repeat = []
};
g["lime.app._Event_lime_ui_Window_Void"] = Sj;
Sj.__name__ = "lime.app._Event_lime_ui_Window_Void";
Sj.prototype = {
    add: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        for (var d = 0, f = this.__priorities.length; d < f;) {
            var h = d++;
            if (c > this.__priorities[h]) {
                this.__listeners.splice(h, 0, a);
                this.__priorities.splice(h, 0, c);
                this.__repeat.splice(h, 0, !b);
                return
            }
        }
        this.__listeners.push(a);
        this.__priorities.push(c);
        this.__repeat.push(!b)
    },
    remove: function(a) {
        for (var b = this.__listeners.length; 0 <= --b;) this.__listeners[b] == a && (this.__listeners.splice(b, 1), this.__priorities.splice(b, 1), this.__repeat.splice(b, 1))
    },
    dispatch: function(a) {
        this.canceled = !1;
        for (var b = this.__listeners, c = this.__repeat, d = 0; d < b.length && (b[d](a), c[d] ? ++d : this.remove(b[d]), !this.canceled););
    },
    __class__: Sj
};
var sg = y["lime.graphics.ImageChannel"] = {
    __ename__: "lime.graphics.ImageChannel",
    __constructs__: null,
    RED: {
        _hx_name: "RED",
        _hx_index: 0,
        __enum__: "lime.graphics.ImageChannel",
        toString: r
    },
    GREEN: {
        _hx_name: "GREEN",
        _hx_index: 1,
        __enum__: "lime.graphics.ImageChannel",
        toString: r
    },
    BLUE: {
        _hx_name: "BLUE",
        _hx_index: 2,
        __enum__: "lime.graphics.ImageChannel",
        toString: r
    },
    ALPHA: {
        _hx_name: "ALPHA",
        _hx_index: 3,
        __enum__: "lime.graphics.ImageChannel",
        toString: r
    }
};
sg.__constructs__ = [sg.RED, sg.GREEN, sg.BLUE, sg.ALPHA];
var si = y["lime.graphics.ImageFileFormat"] = {
    __ename__: "lime.graphics.ImageFileFormat",
    __constructs__: null,
    BMP: {
        _hx_name: "BMP",
        _hx_index: 0,
        __enum__: "lime.graphics.ImageFileFormat",
        toString: r
    },
    JPEG: {
        _hx_name: "JPEG",
        _hx_index: 1,
        __enum__: "lime.graphics.ImageFileFormat",
        toString: r
    },
    PNG: {
        _hx_name: "PNG",
        _hx_index: 2,
        __enum__: "lime.graphics.ImageFileFormat",
        toString: r
    }
};
si.__constructs__ = [si.BMP, si.JPEG, si.PNG];
var qk = function() {};
g["lime.graphics.RenderContext"] = qk;
qk.__name__ = "lime.graphics.RenderContext";
qk.prototype = {
    __class__: qk
};
var Kl = {
        uniform2fv: function(a, b, c, d, f) {
            null != d ? a.uniform2fv(b, c, d, f) : a.uniform2fv(b, c)
        }
    },
    Lc = {
        bufferData: function(a, b, c, d) {
            a.bufferData(b, c, d)
        },
        texImage2D: function(a,
            b, c, d, f, h, k, n, p, g) {
            null != n ? a.texImage2D(b, c, d, f, h, k, n, p, g) : a.texImage2D(b, c, d, f, h, k)
        },
        uniformMatrix2fv: function(a, b, c, d) {
            a.uniformMatrix2fv(b, c, d)
        },
        uniformMatrix3fv: function(a, b, c, d) {
            a.uniformMatrix3fv(b, c, d)
        },
        uniformMatrix4fv: function(a, b, c, d) {
            a.uniformMatrix4fv(b, c, d)
        },
        fromWebGL2RenderContext: function(a) {
            return a
        }
    },
    il = function(a) {};
g["lime.graphics.cairo.Cairo"] = il;
il.__name__ = "lime.graphics.cairo.Cairo";
il.prototype = {
    clip: function() {},
    identityMatrix: function() {},
    newPath: function() {},
    paint: function() {},
    rectangle: function(a, b, c, d) {},
    restore: function() {},
    save: function() {},
    setOperator: function(a) {
        return a
    },
    setSourceRGB: function(a, b, c) {},
    set_matrix: function(a) {
        return a
    },
    __class__: il,
    __properties__: {
        set_matrix: "set_matrix"
    }
};
var El = {
        flush: function(a) {}
    },
    Xe = function() {};
g["lime.graphics.opengl.GL"] = Xe;
Xe.__name__ = "lime.graphics.opengl.GL";
var jl = function() {
    this.STACK_UNDERFLOW = 1284;
    this.STACK_OVERFLOW = 1283;
    this.CONTEXT_FLAG_DEBUG_BIT = 2;
    this.DEBUG_OUTPUT = 37600;
    this.DEBUG_SEVERITY_LOW = 37192;
    this.DEBUG_SEVERITY_MEDIUM =
        37191;
    this.DEBUG_SEVERITY_HIGH = 37190;
    this.DEBUG_LOGGED_MESSAGES = 37189;
    this.MAX_DEBUG_LOGGED_MESSAGES = 37188;
    this.MAX_DEBUG_MESSAGE_LENGTH = 37187;
    this.MAX_LABEL_LENGTH = 33512;
    this.SAMPLER = 33510;
    this.QUERY = 33507;
    this.PROGRAM = 33506;
    this.SHADER = 33505;
    this.BUFFER = 33504;
    this.DEBUG_GROUP_STACK_DEPTH = 33389;
    this.MAX_DEBUG_GROUP_STACK_DEPTH = 33388;
    this.DEBUG_SEVERITY_NOTIFICATION = 33387;
    this.DEBUG_TYPE_POP_GROUP = 33386;
    this.DEBUG_TYPE_PUSH_GROUP = 33385;
    this.DEBUG_TYPE_MARKER = 33384;
    this.DEBUG_TYPE_OTHER = 33361;
    this.DEBUG_TYPE_PERFORMANCE =
        33360;
    this.DEBUG_TYPE_PORTABILITY = 33359;
    this.DEBUG_TYPE_UNDEFINED_BEHAVIOR = 33358;
    this.DEBUG_TYPE_DEPRECATED_BEHAVIOR = 33357;
    this.DEBUG_TYPE_ERROR = 33356;
    this.DEBUG_SOURCE_OTHER = 33355;
    this.DEBUG_SOURCE_APPLICATION = 33354;
    this.DEBUG_SOURCE_THIRD_PARTY = 33353;
    this.DEBUG_SOURCE_SHADER_COMPILER = 33352;
    this.DEBUG_SOURCE_WINDOW_SYSTEM = 33351;
    this.DEBUG_SOURCE_API = 33350;
    this.DEBUG_CALLBACK_USER_PARAM = 33349;
    this.DEBUG_CALLBACK_FUNCTION = 33348;
    this.DEBUG_NEXT_LOGGED_MESSAGE_LENGTH = 33347;
    this.DEBUG_OUTPUT_SYNCHRONOUS =
        33346
};
g["lime.graphics.opengl.ext.KHR_debug"] = jl;
jl.__name__ = "lime.graphics.opengl.ext.KHR_debug";
jl.prototype = {
    __class__: jl
};
var Qc = {
        getAlphaTable: function(a) {
            null == Qc.__alphaTable && (Qc.__alphaTable = new Uint8Array(256));
            Qc.__alphaTable[0] = 0;
            for (var b = 1; 256 > b;) {
                var c = b++;
                var d = Math.floor(c * a[18] + 255 * a[19]);
                255 < d && (d = 255);
                0 > d && (d = 0);
                Qc.__alphaTable[c] = d
            }
            return Qc.__alphaTable
        },
        getBlueTable: function(a) {
            null == Qc.__blueTable && (Qc.__blueTable = new Uint8Array(256));
            for (var b, c = 0; 256 > c;) {
                var d = c++;
                b = Math.floor(d *
                    a[12] + 255 * a[14]);
                255 < b && (b = 255);
                0 > b && (b = 0);
                Qc.__blueTable[d] = b
            }
            return Qc.__blueTable
        },
        getGreenTable: function(a) {
            null == Qc.__greenTable && (Qc.__greenTable = new Uint8Array(256));
            for (var b, c = 0; 256 > c;) {
                var d = c++;
                b = Math.floor(d * a[6] + 255 * a[9]);
                255 < b && (b = 255);
                0 > b && (b = 0);
                Qc.__greenTable[d] = b
            }
            return Qc.__greenTable
        },
        getRedTable: function(a) {
            null == Qc.__redTable && (Qc.__redTable = new Uint8Array(256));
            for (var b, c = 0; 256 > c;) {
                var d = c++;
                b = Math.floor(d * a[0] + 255 * a[4]);
                255 < b && (b = 255);
                0 > b && (b = 0);
                Qc.__redTable[d] = b
            }
            return Qc.__redTable
        },
        __toFlashColorTransform: function(a) {
            return null
        }
    },
    pb = {
        _new: function(a) {
            if (null == a || 16 != a.length) a = pb.__identity, a = null != a ? new Float32Array(a) : null;
            return a
        },
        append: function(a, b) {
            var c = a[0],
                d = a[4],
                f = a[8],
                h = a[12],
                k = a[1],
                n = a[5],
                p = a[9],
                g = a[13],
                q = a[2],
                m = a[6],
                u = a[10],
                r = a[14],
                l = a[3],
                D = a[7],
                x = a[11],
                w = a[15],
                z = pb.get(b, 0),
                J = pb.get(b, 4),
                y = pb.get(b, 8),
                C = pb.get(b, 12),
                t = pb.get(b, 1),
                v = pb.get(b, 5),
                G = pb.get(b, 9),
                F = pb.get(b, 13),
                I = pb.get(b, 2),
                eb = pb.get(b, 6),
                O = pb.get(b, 10),
                H = pb.get(b, 14),
                lb = pb.get(b, 3),
                K = pb.get(b, 7),
                Va = pb.get(b, 11);
            b = pb.get(b, 15);
            a[0] = c * z + k * J + q * y + l * C;
            a[1] = c * t + k * v + q * G + l * F;
            a[2] = c * I + k * eb + q * O + l * H;
            a[3] = c * lb + k * K + q * Va + l * b;
            a[4] = d * z + n * J + m * y + D * C;
            a[5] = d * t + n * v + m * G + D * F;
            a[6] = d * I + n * eb + m * O + D * H;
            a[7] = d * lb + n * K + m * Va + D * b;
            a[8] = f * z + p * J + u * y + x * C;
            a[9] = f * t + p * v + u * G + x * F;
            a[10] = f * I + p * eb + u * O + x * H;
            a[11] = f * lb + p * K + u * Va + x * b;
            a[12] = h * z + g * J + r * y + w * C;
            a[13] = h * t + g * v + r * G + w * F;
            a[14] = h * I + g * eb + r * O + w * H;
            a[15] = h * lb + g * K + r * Va + w * b
        },
        createOrtho: function(a, b, c, d, f, h, k) {
            var n = 1 / (c - b),
                p = 1 / (f - d),
                g = 1 / (k - h);
            a[0] = 2 * n;
            a[1] = 0;
            a[2] = 0;
            a[3] = 0;
            a[4] = 0;
            a[5] = 2 * p;
            a[6] = 0;
            a[7] = 0;
            a[8] = 0;
            a[9] = 0;
            a[10] = -2 * g;
            a[11] = 0;
            a[12] = -(b + c) * n;
            a[13] = -(d + f) * p;
            a[14] = -(h + k) * g;
            a[15] = 1
        },
        identity: function(a) {
            a[0] = 1;
            a[1] = 0;
            a[2] = 0;
            a[3] = 0;
            a[4] = 0;
            a[5] = 1;
            a[6] = 0;
            a[7] = 0;
            a[8] = 0;
            a[9] = 0;
            a[10] = 1;
            a[11] = 0;
            a[12] = 0;
            a[13] = 0;
            a[14] = 0;
            a[15] = 1
        },
        get: function(a, b) {
            return a[b]
        },
        set: function(a, b, c) {
            return a[b] = c
        }
    },
    R = {},
    ok = function(a, b, c, d) {
        null == d && (d = 0);
        null == c && (c = 0);
        null == b && (b = 0);
        null == a && (a = 0);
        this.w = d;
        this.x = a;
        this.y = b;
        this.z = c
    };
g["lime.math.Vector4"] = ok;
ok.__name__ = "lime.math.Vector4";
ok.prototype = {
    __class__: ok
};
var Bc = function() {};
g["lime.media.AudioBuffer"] = Bc;
Bc.__name__ = "lime.media.AudioBuffer";
Bc.fromBytes = function(a) {
    if (null == a) return null;
    var b = new Bc;
    b.set_src(new Howl({
        src: ["data:" + Bc.__getCodec(a) + ";base64," + Be.encode(a)],
        html5: !0,
        preload: !1
    }));
    return b
};
Bc.fromFile = function(a) {
    if (null == a) return null;
    var b = new Bc;
    b.__srcHowl = new Howl({
        src: [a],
        preload: !1
    });
    return b
};
Bc.fromFiles = function(a) {
    var b = new Bc;
    b.__srcHowl = new Howl({
        src: a,
        preload: !1
    });
    return b
};
Bc.loadFromFile = function(a) {
    var b =
        new Gd,
        c = Bc.fromFile(a);
    null != c ? null != c && (c.__srcHowl.on("load", function() {
        b.complete(c)
    }), c.__srcHowl.on("loaderror", function(a, c) {
        b.error(c)
    }), c.__srcHowl.load()) : b.error(null);
    return b.future
};
Bc.loadFromFiles = function(a) {
    var b = new Gd,
        c = Bc.fromFiles(a);
    null != c ? (c.__srcHowl.on("load", function() {
        b.complete(c)
    }), c.__srcHowl.on("loaderror", function() {
        b.error(null)
    }), c.__srcHowl.load()) : b.error(null);
    return b.future
};
Bc.__getCodec = function(a) {
    switch (a.getString(0, 4)) {
        case "OggS":
            return "audio/ogg";
        case "RIFF":
            if ("WAVE" ==
                a.getString(8, 4)) return "audio/wav";
            var b = a.b[1],
                c = a.b[2];
            switch (a.b[0]) {
                case 73:
                    if (68 == b && 51 == c) return "audio/mp3";
                    break;
                case 255:
                    switch (b) {
                        case 243:
                        case 250:
                        case 251:
                            return "audio/mp3"
                    }
            }
            break;
        case "fLaC":
            return "audio/flac";
        default:
            switch (b = a.b[1], c = a.b[2], a.b[0]) {
                case 73:
                    if (68 == b && 51 == c) return "audio/mp3";
                    break;
                case 255:
                    switch (b) {
                        case 243:
                        case 250:
                        case 251:
                            return "audio/mp3"
                    }
            }
    }
    Ga.error("Unsupported sound format", {
        fileName: "lime/media/AudioBuffer.hx",
        lineNumber: 440,
        className: "lime.media.AudioBuffer",
        methodName: "__getCodec"
    });
    return null
};
Bc.prototype = {
    set_src: function(a) {
        return this.__srcHowl = a
    },
    __class__: Bc,
    __properties__: {
        set_src: "set_src"
    }
};
var Ck = function(a) {
    if ("custom" != a) {
        if (null == a || "web" == a) try {
            window.AudioContext = window.AudioContext || window.webkitAudioContext, this.web = new window.AudioContext, this.type = "web"
        } catch (b) {
            Ta.lastError = b
        }
        null == this.web && "web" != a && (this.html5 = new Bk, this.type = "html5")
    } else this.type = "custom"
};
g["lime.media.AudioContext"] = Ck;
Ck.__name__ = "lime.media.AudioContext";
Ck.prototype = {
    __class__: Ck
};
var Ge = function() {};
g["lime.media.AudioManager"] = Ge;
Ge.__name__ = "lime.media.AudioManager";
Ge.init = function(a) {
    if (null == Ge.context) {
        if (null == a && (Ge.context = new Ck, a = Ge.context, "openal" == a.type)) {
            var b = a.openal,
                c = b.openDevice();
            c = b.createContext(c);
            b.makeContextCurrent(c);
            b.processContext(c)
        }
        Ge.context = a
    }
};
var kl = function(a, b, c, d) {
    null == d && (d = 0);
    null == b && (b = 0);
    this.onComplete = new Ac;
    this.buffer = a;
    this.offset = b;
    this.__backend = new pk(this);
    null != c && 0 != c && this.set_length(c);
    this.set_loops(d);
    null != a && this.init()
};
g["lime.media.AudioSource"] = kl;
kl.__name__ = "lime.media.AudioSource";
kl.prototype = {
    dispose: function() {
        this.__backend.dispose()
    },
    init: function() {
        this.__backend.init()
    },
    play: function() {
        this.__backend.play()
    },
    stop: function() {
        this.__backend.stop()
    },
    get_currentTime: function() {
        return this.__backend.getCurrentTime()
    },
    set_currentTime: function(a) {
        return this.__backend.setCurrentTime(a)
    },
    get_gain: function() {
        return this.__backend.getGain()
    },
    set_gain: function(a) {
        return this.__backend.setGain(a)
    },
    set_length: function(a) {
        return this.__backend.setLength(a)
    },
    set_loops: function(a) {
        return this.__backend.setLoops(a)
    },
    get_position: function() {
        return this.__backend.getPosition()
    },
    set_position: function(a) {
        return this.__backend.setPosition(a)
    },
    __class__: kl,
    __properties__: {
        set_loops: "set_loops",
        set_length: "set_length",
        set_position: "set_position",
        get_position: "get_position",
        set_gain: "set_gain",
        get_gain: "get_gain",
        set_currentTime: "set_currentTime",
        get_currentTime: "get_currentTime"
    }
};
var Bk = function() {};
g["lime.media.HTML5AudioContext"] = Bk;
Bk.__name__ = "lime.media.HTML5AudioContext";
Bk.prototype = {
    __class__: Bk
};
var ll = function() {};
g["lime.media.OpenALAudioContext"] = ll;
ll.__name__ = "lime.media.OpenALAudioContext";
ll.prototype = {
    createContext: function(a, b) {
        return sf.createContext(a, b)
    },
    makeContextCurrent: function(a) {
        return sf.makeContextCurrent(a)
    },
    openDevice: function(a) {
        return sf.openDevice(a)
    },
    processContext: function(a) {
        sf.processContext(a)
    },
    __class__: ll
};
var sf = function() {};
g["lime.media.openal.ALC"] =
    sf;
sf.__name__ = "lime.media.openal.ALC";
sf.createContext = function(a, b) {
    return null
};
sf.makeContextCurrent = function(a) {
    return !1
};
sf.openDevice = function(a) {
    return null
};
sf.processContext = function(a) {};
var Ui = function() {};
g["lime.net._IHTTPRequest"] = Ui;
Ui.__name__ = "lime.net._IHTTPRequest";
Ui.__isInterface__ = !0;
Ui.prototype = {
    __class__: Ui
};
var qe = function(a) {
    this.uri = a;
    this.contentType = "application/x-www-form-urlencoded";
    this.followRedirects = !0;
    this.enableResponseHeaders = !1;
    this.formData = new Qa;
    this.headers = [];
    this.method = "GET";
    this.timeout = 3E4;
    this.withCredentials = !1;
    this.manageCookies = !0;
    this.__backend = new Ba;
    this.__backend.init(this)
};
g["lime.net._HTTPRequest.AbstractHTTPRequest"] = qe;
qe.__name__ = "lime.net._HTTPRequest.AbstractHTTPRequest";
qe.__interfaces__ = [Ui];
qe.prototype = {
    __class__: qe
};
var tf = function(a) {
    qe.call(this, a)
};
g["lime.net._HTTPRequest_Bytes"] = tf;
tf.__name__ = "lime.net._HTTPRequest_Bytes";
tf.__super__ = qe;
tf.prototype = v(qe.prototype, {
    fromBytes: function(a) {
        return a
    },
    load: function(a) {
        var b =
            this;
        null != a && (this.uri = a);
        var c = new Gd;
        a = this.__backend.loadData(this.uri);
        a.onProgress(l(c, c.progress));
        a.onError(function(a) {
            b.responseData = a.responseData;
            c.error(a.error)
        });
        a.onComplete(function(a) {
            b.responseData = b.fromBytes(a);
            c.complete(b.responseData)
        });
        return c.future
    },
    __class__: tf
});
var Gg = function(a) {
    qe.call(this, a)
};
g["lime.net._HTTPRequest_String"] = Gg;
Gg.__name__ = "lime.net._HTTPRequest_String";
Gg.__super__ = qe;
Gg.prototype = v(qe.prototype, {
    load: function(a) {
        var b = this;
        null != a && (this.uri =
            a);
        var c = new Gd;
        a = this.__backend.loadText(this.uri);
        a.onProgress(l(c, c.progress));
        a.onError(function(a) {
            b.responseData = a.responseData;
            c.error(a.error)
        });
        a.onComplete(function(a) {
            b.responseData = a;
            c.complete(b.responseData)
        });
        return c.future
    },
    __class__: Gg
});
var Dg = function(a, b) {
    this.error = a;
    this.responseData = b
};
g["lime.net._HTTPRequestErrorResponse"] = Dg;
Dg.__name__ = "lime.net._HTTPRequestErrorResponse";
Dg.prototype = {
    __class__: Dg
};
var Oi = function(a, b) {
    null == b && (b = "");
    this.name = a;
    this.value = b
};
g["lime.net.HTTPRequestHeader"] =
    Oi;
Oi.__name__ = "lime.net.HTTPRequestHeader";
Oi.prototype = {
    __class__: Oi
};
var Vi = function(a) {
    qe.call(this, a)
};
g["lime.net._HTTPRequest_lime_utils_Bytes"] = Vi;
Vi.__name__ = "lime.net._HTTPRequest_lime_utils_Bytes";
Vi.__super__ = tf;
Vi.prototype = v(tf.prototype, {
    fromBytes: function(a) {
        return Tf.fromBytes(a)
    },
    __class__: Vi
});
var Wi = function(a) {
    qe.call(this, a)
};
g["lime.net._HTTPRequest_openfl_utils_ByteArray"] = Wi;
Wi.__name__ = "lime.net._HTTPRequest_openfl_utils_ByteArray";
Wi.__super__ = tf;
Wi.prototype = v(tf.prototype, {
    fromBytes: function(a) {
        return Td.fromBytes(a)
    },
    __class__: Wi
});
var zc = function() {};
g["lime.system.Clipboard"] = zc;
zc.__name__ = "lime.system.Clipboard";
zc.__properties__ = {
    set_text: "set_text",
    get_text: "get_text"
};
zc.__update = function() {
    var a = zc._text;
    zc._text = a;
    zc.__updated = !0;
    zc._text != a && zc.onUpdate.dispatch()
};
zc.get_text = function() {
    zc.__update();
    return zc._text
};
zc.set_text = function(a) {
    var b = zc._text;
    zc._text = a;
    var c = A.current.__window;
    null != c && c.__backend.setClipboard(a);
    zc._text != b && zc.onUpdate.dispatch();
    return a
};
var Xi = y["lime.system.Endian"] = {
    __ename__: "lime.system.Endian",
    __constructs__: null,
    LITTLE_ENDIAN: {
        _hx_name: "LITTLE_ENDIAN",
        _hx_index: 0,
        __enum__: "lime.system.Endian",
        toString: r
    },
    BIG_ENDIAN: {
        _hx_name: "BIG_ENDIAN",
        _hx_index: 1,
        __enum__: "lime.system.Endian",
        toString: r
    }
};
Xi.__constructs__ = [Xi.LITTLE_ENDIAN, Xi.BIG_ENDIAN];
var He = function(a, b) {
    this.onUpdate = new rk;
    this.type = a;
    this.id = b
};
g["lime.system.Sensor"] = He;
He.__name__ = "lime.system.Sensor";
He.registerSensor = function(a, b) {
    a = new He(a, b);
    He.sensors.push(a);
    return He.sensorByID.h[b] = a
};
He.prototype = {
    __class__: He
};
var tl = y["lime.system.SensorType"] = {
    __ename__: "lime.system.SensorType",
    __constructs__: null,
    ACCELEROMETER: {
        _hx_name: "ACCELEROMETER",
        _hx_index: 0,
        __enum__: "lime.system.SensorType",
        toString: r
    }
};
tl.__constructs__ = [tl.ACCELEROMETER];
var Cc = function() {};
g["lime.system.System"] = Cc;
Cc.__name__ = "lime.system.System";
Cc.__properties__ = {
    get_endianness: "get_endianness"
};
Cc.embed = t.lime.embed = function(a, b, c, d, f) {
    if (null != Cc.__applicationEntryPoint && Object.prototype.hasOwnProperty.call(Cc.__applicationEntryPoint.h,
            a)) {
        var h = "string" == typeof b ? window.document.getElementById(b) : null == b ? window.document.createElement("div") : b;
        null == h ? window.console.log("[lime.embed] ERROR: Cannot find target element: " + H.string(b)) : (null == c && (c = 0), null == d && (d = 0), null == f && (f = {}), Object.prototype.hasOwnProperty.call(f, "background") && "string" == typeof f.background && (b = O.replace(H.string(f.background), "#", ""), -1 < b.indexOf("0x") ? f.background = H.parseInt(b) : f.background = H.parseInt("0x" + b)), f.element = h, f.width = c, f.height = d, Cc.__applicationEntryPoint.h[a](f))
    }
};
Cc.exit = function(a) {
    var b = A.current;
    if (null != b && (b.onExit.dispatch(a), b.onExit.canceled)) return;
    null != b && null != b.__window && b.__window.close()
};
Cc.getTimer = function() {
    return window.performance.now() | 0
};
Cc.openURL = function(a, b) {
    null == b && (b = "_blank");
    null != a && window.open(a, b)
};
Cc.__registerEntryPoint = function(a, b) {
    null == Cc.__applicationEntryPoint && (Cc.__applicationEntryPoint = new Qa);
    Cc.__applicationEntryPoint.h[a] = b
};
Cc.get_endianness = function() {
    if (null == Cc.__endianness) {
        var a = new ArrayBuffer(2),
            b = null,
            c = null,
            d = null,
            f = null,
            h = null,
            k = b = null != b ? new Uint8Array(b) : null != c ? new Uint8Array(c) : null != d ? new Uint8Array(d.__array) : null != f ? new Uint8Array(f) : null != a ? null == h ? new Uint8Array(a, 0) : new Uint8Array(a, 0, h) : null;
        h = f = d = c = b = null;
        b = null != b ? new Uint16Array(b) : null != c ? new Uint16Array(c) : null != d ? new Uint16Array(d.__array) : null != f ? new Uint16Array(f) : null != a ? null == h ? new Uint16Array(a, 0) : new Uint16Array(a, 0, h) : null;
        k[0] = 170;
        k[1] = 187;
        Cc.__endianness = 43707 == b[0] ? Xi.BIG_ENDIAN : Xi.LITTLE_ENDIAN
    }
    return Cc.__endianness
};
var qc = function(a) {
    this.onDisconnect = new Ac;
    this.onButtonUp = new Si;
    this.onButtonDown = new Si;
    this.onAxisMove = new yk;
    this.id = a;
    this.connected = !0
};
g["lime.ui.Gamepad"] = qc;
qc.__name__ = "lime.ui.Gamepad";
qc.__connect = function(a) {
    if (!qc.devices.h.hasOwnProperty(a)) {
        var b = new qc(a);
        qc.devices.h[a] = b;
        qc.onConnect.dispatch(b)
    }
};
qc.__disconnect = function(a) {
    var b = qc.devices.h[a];
    null != b && (b.connected = !1);
    qc.devices.remove(a);
    null != b && b.onDisconnect.dispatch()
};
qc.prototype = {
    __class__: qc
};
var nc = function(a) {
    this.onHatMove =
        new wk;
    this.onDisconnect = new Ac;
    this.onButtonUp = new zf;
    this.onButtonDown = new zf;
    this.onAxisMove = new vk;
    this.id = a;
    this.connected = !0
};
g["lime.ui.Joystick"] = nc;
nc.__name__ = "lime.ui.Joystick";
nc.__connect = function(a) {
    if (!nc.devices.h.hasOwnProperty(a)) {
        var b = new nc(a);
        nc.devices.h[a] = b;
        nc.onConnect.dispatch(b)
    }
};
nc.__disconnect = function(a) {
    var b = nc.devices.h[a];
    null != b && (b.connected = !1);
    nc.devices.remove(a);
    null != b && b.onDisconnect.dispatch()
};
nc.__getDeviceData = function() {
    var a = null;
    try {
        a = navigator.getGamepads ?
            navigator.getGamepads() : navigator.webkitGetGamepads ? navigator.webkitGetGamepads() : null
    } catch (b) {
        Ta.lastError = b
    }
    return a
};
nc.prototype = {
    __class__: nc
};
var Xa = {
        __properties__: {
            get_shiftKey: "get_shiftKey",
            get_metaKey: "get_metaKey",
            get_ctrlKey: "get_ctrlKey",
            get_altKey: "get_altKey"
        },
        get_altKey: function(a) {
            return 0 >= (a & 256) ? 0 < (a & 512) : !0
        },
        get_ctrlKey: function(a) {
            return 0 >= (a & 64) ? 0 < (a & 128) : !0
        },
        get_metaKey: function(a) {
            return 0 >= (a & 1024) ? 0 < (a & 2048) : !0
        },
        get_shiftKey: function(a) {
            return 0 >= (a & 1) ? 0 < (a & 2) : !0
        }
    },
    cc =
    y["lime.ui.MouseCursor"] = {
        __ename__: "lime.ui.MouseCursor",
        __constructs__: null,
        ARROW: {
            _hx_name: "ARROW",
            _hx_index: 0,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        CROSSHAIR: {
            _hx_name: "CROSSHAIR",
            _hx_index: 1,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        DEFAULT: {
            _hx_name: "DEFAULT",
            _hx_index: 2,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        MOVE: {
            _hx_name: "MOVE",
            _hx_index: 3,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        POINTER: {
            _hx_name: "POINTER",
            _hx_index: 4,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        RESIZE_NESW: {
            _hx_name: "RESIZE_NESW",
            _hx_index: 5,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        RESIZE_NS: {
            _hx_name: "RESIZE_NS",
            _hx_index: 6,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        RESIZE_NWSE: {
            _hx_name: "RESIZE_NWSE",
            _hx_index: 7,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        RESIZE_WE: {
            _hx_name: "RESIZE_WE",
            _hx_index: 8,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        TEXT: {
            _hx_name: "TEXT",
            _hx_index: 9,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        WAIT: {
            _hx_name: "WAIT",
            _hx_index: 10,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        WAIT_ARROW: {
            _hx_name: "WAIT_ARROW",
            _hx_index: 11,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        },
        CUSTOM: {
            _hx_name: "CUSTOM",
            _hx_index: 12,
            __enum__: "lime.ui.MouseCursor",
            toString: r
        }
    };
cc.__constructs__ = [cc.ARROW, cc.CROSSHAIR, cc.DEFAULT, cc.MOVE, cc.POINTER, cc.RESIZE_NESW, cc.RESIZE_NS, cc.RESIZE_NWSE, cc.RESIZE_WE, cc.TEXT, cc.WAIT, cc.WAIT_ARROW, cc.CUSTOM];
var rf = y["lime.ui.MouseWheelMode"] = {
    __ename__: "lime.ui.MouseWheelMode",
    __constructs__: null,
    PIXELS: {
        _hx_name: "PIXELS",
        _hx_index: 0,
        __enum__: "lime.ui.MouseWheelMode",
        toString: r
    },
    LINES: {
        _hx_name: "LINES",
        _hx_index: 1,
        __enum__: "lime.ui.MouseWheelMode",
        toString: r
    },
    PAGES: {
        _hx_name: "PAGES",
        _hx_index: 2,
        __enum__: "lime.ui.MouseWheelMode",
        toString: r
    },
    UNKNOWN: {
        _hx_name: "UNKNOWN",
        _hx_index: 3,
        __enum__: "lime.ui.MouseWheelMode",
        toString: r
    }
};
rf.__constructs__ = [rf.PIXELS, rf.LINES, rf.PAGES, rf.UNKNOWN];
var dc = function(a, b, c, d, f, h, k) {
    this.x = a;
    this.y = b;
    this.id = c;
    this.dx = d;
    this.dy = f;
    this.pressure = h;
    this.device = k
};
g["lime.ui.Touch"] = dc;
dc.__name__ = "lime.ui.Touch";
dc.prototype = {
    __class__: dc
};
var Hg = function(a, b) {
    this.clickCount =
        0;
    this.onTextInput = new Qi;
    this.onTextEdit = new xk;
    this.onRestore = new Ac;
    this.onResize = new Pi;
    this.onRenderContextRestored = new Ri;
    this.onRenderContextLost = new Ac;
    this.onRender = new Ri;
    this.onMove = new uh;
    this.onMouseWheel = new uk;
    this.onMouseUp = new sk;
    this.onMouseMoveRelative = new uh;
    this.onMouseMove = new uh;
    this.onMouseDown = new tk;
    this.onMinimize = new Ac;
    this.onLeave = new Ac;
    this.onKeyUp = new Ti;
    this.onKeyDown = new Ti;
    this.onFullscreen = new Ac;
    this.onFocusOut = new Ac;
    this.onFocusIn = new Ac;
    this.onExpose = new Ac;
    this.onEnter = new Ac;
    this.onDropFile = new Qi;
    this.onDeactivate = new Ac;
    this.onClose = new Ac;
    this.onActivate = new Ac;
    this.application = a;
    this.__attributes = null != b ? b : {};
    Object.prototype.hasOwnProperty.call(this.__attributes, "parameters") && (this.parameters = this.__attributes.parameters);
    this.__height = this.__width = 0;
    this.__fullscreen = !1;
    this.__scale = 1;
    this.__y = this.__x = 0;
    this.__title = Object.prototype.hasOwnProperty.call(this.__attributes, "title") ? this.__attributes.title : "";
    this.id = -1;
    this.__backend = new Ha(this)
};
g["lime.ui.Window"] = Hg;
Hg.__name__ = "lime.ui.Window";
Hg.prototype = {
    close: function() {
        this.__backend.close()
    },
    set_cursor: function(a) {
        return this.__backend.setCursor(a)
    },
    setTextInputRect: function(a) {
        return this.__backend.setTextInputRect(a)
    },
    __class__: Hg,
    __properties__: {
        set_cursor: "set_cursor"
    }
};
var re = function() {
    this.data = new Qa;
    this.paths = []
};
g["lime.utils.AssetBundle"] = re;
re.__name__ = "lime.utils.AssetBundle";
re.fromBytes = function(a) {
    a = new Ni(a);
    return re.__extractBundle(a)
};
re.loadFromBytes = function(a) {
    return hc.withValue(re.fromBytes(a))
};
re.loadFromFile = function(a) {
    return Tf.loadFromFile(a).then(re.loadFromBytes)
};
re.__extractBundle = function(a) {
    var b = th.readZip(a);
    a = new re;
    for (b = b.h; null != b;) {
        var c = b.item;
        b = b.next;
        if (c.compressed) {
            var d = a.data,
                f = c.fileName,
                h = Tf.decompress(c.data, Yi.DEFLATE);
            d.h[f] = h
        } else a.data.h[c.fileName] = c.data;
        a.paths.push(c.fileName)
    }
    return a
};
re.prototype = {
    __class__: re
};
var Dk = function() {
    this.enabled = !0;
    this.audio = new Qa;
    this.font = new Qa;
    this.image = new Qa;
    this.version = 575701
};
g["lime.utils.AssetCache"] = Dk;
Dk.__name__ = "lime.utils.AssetCache";
Dk.prototype = {
    exists: function(a, b) {
        return ("IMAGE" == b || null == b) && Object.prototype.hasOwnProperty.call(this.image.h, a) || ("FONT" == b || null == b) && Object.prototype.hasOwnProperty.call(this.font.h, a) || ("SOUND" == b || "MUSIC" == b || null == b) && Object.prototype.hasOwnProperty.call(this.audio.h, a) ? !0 : !1
    },
    set: function(a, b, c) {
        switch (b) {
            case "FONT":
                this.font.h[a] = c;
                break;
            case "IMAGE":
                if (!(c instanceof Xb)) throw X.thrown("Cannot cache non-Image asset: " + H.string(c) + " as Image");
                this.image.h[a] =
                    c;
                break;
            case "MUSIC":
            case "SOUND":
                if (!(c instanceof Bc)) throw X.thrown("Cannot cache non-AudioBuffer asset: " + H.string(c) + " as AudioBuffer");
                this.audio.h[a] = c;
                break;
            default:
                throw X.thrown(b + " assets are not cachable");
        }
    },
    clear: function(a) {
        if (null == a) this.audio = new Qa, this.font = new Qa, this.image = new Qa;
        else {
            var b = this.audio.h;
            b = Object.keys(b);
            for (var c = b.length, d = 0; d < c;) {
                var f = b[d++];
                if (O.startsWith(f, a)) {
                    var h = this.audio;
                    Object.prototype.hasOwnProperty.call(h.h, f) && delete h.h[f]
                }
            }
            b = this.font.h;
            b =
                Object.keys(b);
            c = b.length;
            for (d = 0; d < c;) f = b[d++], O.startsWith(f, a) && (h = this.font, Object.prototype.hasOwnProperty.call(h.h, f) && delete h.h[f]);
            b = this.image.h;
            b = Object.keys(b);
            c = b.length;
            for (d = 0; d < c;) f = b[d++], O.startsWith(f, a) && (h = this.image, Object.prototype.hasOwnProperty.call(h.h, f) && delete h.h[f])
        }
    },
    __class__: Dk
};
var Wb = function() {
    this.types = new Qa;
    this.sizes = new Qa;
    this.preload = new Qa;
    this.paths = new Qa;
    this.pathGroups = new Qa;
    this.classTypes = new Qa;
    this.cachedText = new Qa;
    this.cachedImages = new Qa;
    this.cachedFonts =
        new Qa;
    this.cachedBytes = new Qa;
    this.cachedAudioBuffers = new Qa;
    this.onChange = new Ac;
    this.bytesTotal = this.bytesLoaded = 0
};
g["lime.utils.AssetLibrary"] = Wb;
Wb.__name__ = "lime.utils.AssetLibrary";
Wb.fromBundle = function(a) {
    if (Object.prototype.hasOwnProperty.call(a.data.h, "library.json")) {
        var b = td.fromBytes(a.data.h["library.json"]);
        if (null != b) {
            if (null == b.libraryType) var c = new Wb;
            else if (c = g[b.libraryType], null != c) c = w.createInstance(c, b.libraryArgs);
            else return Ga.warn("Could not find library type: " + b.libraryType, {
                fileName: "lime/utils/AssetLibrary.hx",
                lineNumber: 122,
                className: "lime.utils.AssetLibrary",
                methodName: "fromBundle"
            }), null;
            c.__fromBundle(a, b);
            return c
        }
    } else return c = new Wb, c.__fromBundle(a), c;
    return null
};
Wb.fromManifest = function(a) {
    if (null == a) return null;
    if (null == a.libraryType) var b = new Wb;
    else if (b = g[a.libraryType], null != b) b = w.createInstance(b, a.libraryArgs);
    else return Ga.warn("Could not find library type: " + a.libraryType, {
        fileName: "lime/utils/AssetLibrary.hx",
        lineNumber: 160,
        className: "lime.utils.AssetLibrary",
        methodName: "fromManifest"
    }), null;
    b.__fromManifest(a);
    return b
};
Wb.prototype = {
    exists: function(a, b) {
        b = null != b ? va.__cast(b, String) : null;
        a = this.types.h[a];
        return null == a || a != b && ("SOUND" != b && "MUSIC" != b || "MUSIC" != a && "SOUND" != a) && "BINARY" != b && null != b && ("BINARY" != a || "TEXT" != b) ? !1 : !0
    },
    getAsset: function(a, b) {
        switch (b) {
            case "BINARY":
                return this.getBytes(a);
            case "FONT":
                return this.getFont(a);
            case "IMAGE":
                return this.getImage(a);
            case "MUSIC":
            case "SOUND":
                return this.getAudioBuffer(a);
            case "TEMPLATE":
                throw X.thrown("Not sure how to get template: " +
                    a);
            case "TEXT":
                return this.getText(a);
            default:
                throw X.thrown("Unknown asset type: " + b);
        }
    },
    getAudioBuffer: function(a) {
        return Object.prototype.hasOwnProperty.call(this.cachedAudioBuffers.h, a) ? this.cachedAudioBuffers.h[a] : Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ? Bc.fromBytes(va.__cast(w.createInstance(this.classTypes.h[a], []), zb)) : Bc.fromFile(this.getPath(a))
    },
    getBytes: function(a) {
        if (Object.prototype.hasOwnProperty.call(this.cachedBytes.h, a)) return this.cachedBytes.h[a];
        if (Object.prototype.hasOwnProperty.call(this.cachedText.h,
                a)) {
            var b = Tf.ofString(this.cachedText.h[a]);
            return this.cachedBytes.h[a] = b
        }
        return Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ? va.__cast(w.createInstance(this.classTypes.h[a], []), zb) : Tf.fromFile(this.getPath(a))
    },
    getFont: function(a) {
        return Object.prototype.hasOwnProperty.call(this.cachedFonts.h, a) ? this.cachedFonts.h[a] : Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ? va.__cast(w.createInstance(this.classTypes.h[a], []), ib) : ib.fromFile(this.getPath(a))
    },
    getImage: function(a) {
        return Object.prototype.hasOwnProperty.call(this.cachedImages.h,
            a) ? this.cachedImages.h[a] : Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ? va.__cast(w.createInstance(this.classTypes.h[a], []), Xb) : Xb.fromFile(this.getPath(a))
    },
    getPath: function(a) {
        return Object.prototype.hasOwnProperty.call(this.paths.h, a) ? this.paths.h[a] : Object.prototype.hasOwnProperty.call(this.pathGroups.h, a) ? this.pathGroups.h[a][0] : null
    },
    getText: function(a) {
        if (Object.prototype.hasOwnProperty.call(this.cachedText.h, a)) return this.cachedText.h[a];
        a = this.getBytes(a);
        return null == a ? null :
            a.getString(0, a.length)
    },
    isLocal: function(a, b) {
        if (Object.prototype.hasOwnProperty.call(this.classTypes.h, a)) return !0;
        b = va.__cast(b, String);
        if (null == b) return Object.prototype.hasOwnProperty.call(this.cachedBytes.h, a) || Object.prototype.hasOwnProperty.call(this.cachedText.h, a) || Object.prototype.hasOwnProperty.call(this.cachedImages.h, a) || Object.prototype.hasOwnProperty.call(this.cachedAudioBuffers.h, a) ? !0 : Object.prototype.hasOwnProperty.call(this.cachedFonts.h, a);
        switch (b) {
            case "FONT":
                return Object.prototype.hasOwnProperty.call(this.cachedFonts.h,
                    a);
            case "IMAGE":
                return Object.prototype.hasOwnProperty.call(this.cachedImages.h, a);
            case "MUSIC":
            case "SOUND":
                return Object.prototype.hasOwnProperty.call(this.cachedAudioBuffers.h, a);
            default:
                return Object.prototype.hasOwnProperty.call(this.cachedBytes.h, a) ? !0 : Object.prototype.hasOwnProperty.call(this.cachedText.h, a)
        }
    },
    load: function() {
        if (this.loaded) return hc.withValue(this);
        if (null == this.promise) {
            this.promise = new Gd;
            this.bytesLoadedCache = new Qa;
            this.assetsLoaded = 0;
            this.assetsTotal = 1;
            for (var a = Object.keys(this.preload.h),
                    b = a.length, c = 0; c < b;) {
                var d = a[c++];
                if (this.preload.h[d]) {
                    Ga.verbose("Preloading asset: " + d + " [" + this.types.h[d] + "]", {
                        fileName: "lime/utils/AssetLibrary.hx",
                        lineNumber: 408,
                        className: "lime.utils.AssetLibrary",
                        methodName: "load"
                    });
                    var f = this.types.h[d];
                    if (null != f) switch (f) {
                        case "BINARY":
                            this.assetsTotal++;
                            f = this.loadBytes(d);
                            f.onProgress(function(a, b) {
                                return function(c, d) {
                                    b[0].load_onProgress(a[0], c, d)
                                }
                            }([d], [this]));
                            f.onError(function(a, b) {
                                return function(c) {
                                    b[0].load_onError(a[0], c)
                                }
                            }([d], [this]));
                            f.onComplete(function(a,
                                b) {
                                return function(c) {
                                    b[0].loadBytes_onComplete(a[0], c)
                                }
                            }([d], [this]));
                            break;
                        case "FONT":
                            this.assetsTotal++;
                            f = this.loadFont(d);
                            f.onProgress(function(a, b) {
                                return function(c, d) {
                                    b[0].load_onProgress(a[0], c, d)
                                }
                            }([d], [this]));
                            f.onError(function(a, b) {
                                return function(c) {
                                    b[0].load_onError(a[0], c)
                                }
                            }([d], [this]));
                            f.onComplete(function(a, b) {
                                return function(c) {
                                    b[0].loadFont_onComplete(a[0], c)
                                }
                            }([d], [this]));
                            break;
                        case "IMAGE":
                            this.assetsTotal++;
                            f = this.loadImage(d);
                            f.onProgress(function(a, b) {
                                return function(c,
                                    d) {
                                    b[0].load_onProgress(a[0], c, d)
                                }
                            }([d], [this]));
                            f.onError(function(a, b) {
                                return function(c) {
                                    b[0].load_onError(a[0], c)
                                }
                            }([d], [this]));
                            f.onComplete(function(a, b) {
                                return function(c) {
                                    b[0].loadImage_onComplete(a[0], c)
                                }
                            }([d], [this]));
                            break;
                        case "MUSIC":
                        case "SOUND":
                            this.assetsTotal++;
                            f = this.loadAudioBuffer(d);
                            f.onProgress(function(a, b) {
                                return function(c, d) {
                                    b[0].load_onProgress(a[0], c, d)
                                }
                            }([d], [this]));
                            f.onError(function(a, b) {
                                return function(c) {
                                    b[0].loadAudioBuffer_onError(a[0], c)
                                }
                            }([d], [this]));
                            f.onComplete(function(a,
                                b) {
                                return function(c) {
                                    b[0].loadAudioBuffer_onComplete(a[0], c)
                                }
                            }([d], [this]));
                            break;
                        case "TEXT":
                            this.assetsTotal++, f = this.loadText(d), f.onProgress(function(a, b) {
                                return function(c, d) {
                                    b[0].load_onProgress(a[0], c, d)
                                }
                            }([d], [this])), f.onError(function(a, b) {
                                return function(c) {
                                    b[0].load_onError(a[0], c)
                                }
                            }([d], [this])), f.onComplete(function(a, b) {
                                return function(c) {
                                    b[0].loadText_onComplete(a[0], c)
                                }
                            }([d], [this]))
                    }
                }
            }
            this.__assetLoaded(null)
        }
        return this.promise.future
    },
    loadAudioBuffer: function(a) {
        return Object.prototype.hasOwnProperty.call(this.cachedAudioBuffers.h,
            a) ? hc.withValue(this.cachedAudioBuffers.h[a]) : Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ? hc.withValue(Bc.fromBytes(va.__cast(w.createInstance(this.classTypes.h[a], []), zb))) : Object.prototype.hasOwnProperty.call(this.pathGroups.h, a) ? Bc.loadFromFiles(this.pathGroups.h[a]) : Bc.loadFromFile(this.paths.h[a])
    },
    loadBytes: function(a) {
        return Object.prototype.hasOwnProperty.call(this.cachedBytes.h, a) ? hc.withValue(this.cachedBytes.h[a]) : Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ?
            hc.withValue(w.createInstance(this.classTypes.h[a], [])) : Tf.loadFromFile(this.getPath(a))
    },
    loadFont: function(a) {
        return Object.prototype.hasOwnProperty.call(this.cachedFonts.h, a) ? hc.withValue(this.cachedFonts.h[a]) : Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ? (a = w.createInstance(this.classTypes.h[a], []), a.__loadFromName(a.name)) : ib.loadFromName(this.getPath(a))
    },
    loadImage: function(a) {
        var b = this;
        return Object.prototype.hasOwnProperty.call(this.cachedImages.h, a) ? hc.withValue(this.cachedImages.h[a]) :
            Object.prototype.hasOwnProperty.call(this.classTypes.h, a) ? hc.withValue(w.createInstance(this.classTypes.h[a], [])) : Object.prototype.hasOwnProperty.call(this.cachedBytes.h, a) ? Xb.loadFromBytes(this.cachedBytes.h[a]).then(function(c) {
                var d = b.cachedBytes;
                Object.prototype.hasOwnProperty.call(d.h, a) && delete d.h[a];
                b.cachedImages.h[a] = c;
                return hc.withValue(c)
            }) : Xb.loadFromFile(this.getPath(a))
    },
    loadText: function(a) {
        if (Object.prototype.hasOwnProperty.call(this.cachedText.h, a)) return hc.withValue(this.cachedText.h[a]);
        if (Object.prototype.hasOwnProperty.call(this.cachedBytes.h, a) || Object.prototype.hasOwnProperty.call(this.classTypes.h, a)) {
            var b = this.getBytes(a);
            if (null == b) return hc.withValue(null);
            b = b.getString(0, b.length);
            this.cachedText.h[a] = b;
            return hc.withValue(b)
        }
        return (new Gg).load(this.getPath(a))
    },
    unload: function() {
        this.cachedBytes.h = Object.create(null);
        this.cachedFonts.h = Object.create(null);
        this.cachedImages.h = Object.create(null);
        this.cachedAudioBuffers.h = Object.create(null);
        this.cachedText.h = Object.create(null)
    },
    __assetLoaded: function(a) {
        this.assetsLoaded++;
        null != a && Ga.verbose("Loaded asset: " + a + " [" + this.types.h[a] + "] (" + (this.assetsLoaded - 1) + "/" + (this.assetsTotal - 1) + ")", {
            fileName: "lime/utils/AssetLibrary.hx",
            lineNumber: 637,
            className: "lime.utils.AssetLibrary",
            methodName: "__assetLoaded"
        });
        if (null != a) {
            var b = Object.prototype.hasOwnProperty.call(this.sizes.h, a) ? this.sizes.h[a] : 0;
            if (Object.prototype.hasOwnProperty.call(this.bytesLoadedCache.h, a)) {
                var c = this.bytesLoadedCache.h[a];
                c < b && (this.bytesLoaded += b - c)
            } else this.bytesLoaded +=
                b;
            this.bytesLoadedCache.h[a] = b
        }
        this.assetsLoaded < this.assetsTotal ? this.promise.progress(this.bytesLoaded, this.bytesTotal) : (this.loaded = !0, this.promise.progress(this.bytesTotal, this.bytesTotal), this.promise.complete(this))
    },
    __cacheBreak: function(a) {
        return Fa.__cacheBreak(a)
    },
    __fromBundle: function(a, b) {
        if (null != b) {
            var c = 0;
            for (b = b.assets; c < b.length;) {
                var d = b[c];
                ++c;
                var f = Object.prototype.hasOwnProperty.call(d, "id") ? d.id : d.path;
                var h = a.data.h[d.path];
                if (Object.prototype.hasOwnProperty.call(d, "type")) {
                    var k =
                        d.type;
                    "TEXT" == k ? this.cachedText.h[f] = null != h ? H.string(h) : null : this.cachedBytes.h[f] = h;
                    this.types.h[f] = d.type
                } else this.cachedBytes.h[f] = h, this.types.h[f] = "BINARY"
            }
        } else
            for (c = 0, b = a.paths; c < b.length;) f = b[c], ++c, this.cachedBytes.h[f] = a.data.h[f], this.types.h[f] = "BINARY"
    },
    __fromManifest: function(a) {
        var b = 2 <= a.version,
            c = a.rootPath;
        null == c && (c = "");
        "" != c && (c += "/");
        for (var d = 0, f = a.assets; d < f.length;) {
            var h = f[d];
            ++d;
            var k = b && Object.prototype.hasOwnProperty.call(h, "size") ? h.size : 100;
            var n = Object.prototype.hasOwnProperty.call(h,
                "id") ? h.id : h.path;
            if (Object.prototype.hasOwnProperty.call(h, "path")) {
                var p = this.paths;
                var q = this.__cacheBreak(this.__resolvePath(c + H.string(ya.field(h, "path"))));
                p.h[n] = q
            }
            if (Object.prototype.hasOwnProperty.call(h, "pathGroup")) {
                p = ya.field(h, "pathGroup");
                q = 0;
                for (var u = p.length; q < u;) {
                    var m = q++;
                    p[m] = this.__cacheBreak(this.__resolvePath(c + p[m]))
                }
                this.pathGroups.h[n] = p
            }
            this.sizes.h[n] = k;
            this.types.h[n] = h.type;
            Object.prototype.hasOwnProperty.call(h, "preload") && (this.preload.h[n] = ya.field(h, "preload"));
            Object.prototype.hasOwnProperty.call(h,
                "className") && (k = ya.field(h, "className"), k = g[k], this.classTypes.h[n] = k)
        }
        d = this.bytesTotal = 0;
        for (f = a.assets; d < f.length;) h = f[d], ++d, n = Object.prototype.hasOwnProperty.call(h, "id") ? h.id : h.path, Object.prototype.hasOwnProperty.call(this.preload.h, n) && this.preload.h[n] && Object.prototype.hasOwnProperty.call(this.sizes.h, n) && (this.bytesTotal += this.sizes.h[n])
    },
    __resolvePath: function(a) {
        a = O.replace(a, "\\", "/");
        var b = a.indexOf(":");
        O.startsWith(a, "http") && 0 < b ? (b += 3, a = N.substr(a, 0, b) + O.replace(N.substr(a, b, null),
            "//", "/")) : a = O.replace(a, "//", "/");
        if (-1 < a.indexOf("./")) {
            a = a.split("/");
            b = [];
            for (var c = 0, d = a.length; c < d;) {
                var f = c++;
                ".." == a[f] ? 0 == f || ".." == b[f - 1] ? b.push("..") : b.pop() : "." == a[f] ? 0 == f && b.push(".") : b.push(a[f])
            }
            a = b.join("/")
        }
        return a
    },
    loadAudioBuffer_onComplete: function(a, b) {
        this.cachedAudioBuffers.h[a] = b;
        if (Object.prototype.hasOwnProperty.call(this.pathGroups.h, a))
            for (var c = this.pathGroups.h[a], d = Object.keys(this.pathGroups.h), f = d.length, h = 0; h < f;) {
                var k = d[h++];
                if (k != a)
                    for (var n = 0; n < c.length;) {
                        var p =
                            c[n];
                        ++n;
                        if (-1 < this.pathGroups.h[k].indexOf(p)) {
                            this.cachedAudioBuffers.h[k] = b;
                            break
                        }
                    }
            }
        this.__assetLoaded(a)
    },
    loadAudioBuffer_onError: function(a, b) {
        null != b && "" != b ? Ga.warn('Could not load "' + a + '": ' + H.string(b), {
            fileName: "lime/utils/AssetLibrary.hx",
            lineNumber: 883,
            className: "lime.utils.AssetLibrary",
            methodName: "loadAudioBuffer_onError"
        }) : Ga.warn('Could not load "' + a + '"', {
            fileName: "lime/utils/AssetLibrary.hx",
            lineNumber: 887,
            className: "lime.utils.AssetLibrary",
            methodName: "loadAudioBuffer_onError"
        });
        this.loadAudioBuffer_onComplete(a, new Bc)
    },
    loadBytes_onComplete: function(a, b) {
        this.cachedBytes.h[a] = b;
        this.__assetLoaded(a)
    },
    loadFont_onComplete: function(a, b) {
        this.cachedFonts.h[a] = b;
        this.__assetLoaded(a)
    },
    loadImage_onComplete: function(a, b) {
        this.cachedImages.h[a] = b;
        this.__assetLoaded(a)
    },
    loadText_onComplete: function(a, b) {
        this.cachedText.h[a] = b;
        this.__assetLoaded(a)
    },
    load_onError: function(a, b) {
        null != b && "" != b ? this.promise.error('Error loading asset "' + a + '": ' + H.string(b)) : this.promise.error('Error loading asset "' +
            a + '"')
    },
    load_onProgress: function(a, b, c) {
        if (0 < b) {
            var d = this.sizes.h[a];
            0 < c ? (b /= c, 1 < b && (b = 1), b = Math.floor(b * d)) : b > d && (b = d);
            Object.prototype.hasOwnProperty.call(this.bytesLoadedCache.h, a) ? (d = this.bytesLoadedCache.h[a], b != d && (this.bytesLoaded += b - d)) : this.bytesLoaded += b;
            this.bytesLoadedCache.h[a] = b;
            this.promise.progress(this.bytesLoaded, this.bytesTotal)
        }
    },
    __class__: Wb
};
var td = function() {
    this.assets = [];
    this.libraryArgs = [];
    this.version = 2
};
g["lime.utils.AssetManifest"] = td;
td.__name__ = "lime.utils.AssetManifest";
td.fromBytes = function(a, b) {
    return null != a ? td.parse(a.getString(0, a.length), b) : null
};
td.loadFromFile = function(a, b) {
    a = td.__resolvePath(a);
    b = td.__resolveRootPath(b, a);
    return null == a ? null : Tf.loadFromFile(a).then(function(a) {
        return hc.withValue(td.fromBytes(a, b))
    })
};
td.parse = function(a, b) {
    if (null == a || "" == a) return null;
    a = JSON.parse(a);
    var c = new td;
    Object.prototype.hasOwnProperty.call(a, "name") && (c.name = a.name);
    Object.prototype.hasOwnProperty.call(a, "libraryType") && (c.libraryType = a.libraryType);
    Object.prototype.hasOwnProperty.call(a,
        "libraryArgs") && (c.libraryArgs = a.libraryArgs);
    if (Object.prototype.hasOwnProperty.call(a, "assets")) {
        var d = a.assets;
        Object.prototype.hasOwnProperty.call(a, "version") && 2 >= a.version ? c.assets = pd.run(d) : c.assets = d
    }
    Object.prototype.hasOwnProperty.call(a, "rootPath") && (c.rootPath = a.rootPath);
    null != b && "" != b && (c.rootPath = null == c.rootPath || "" == c.rootPath ? b : b + "/" + c.rootPath);
    return c
};
td.__resolvePath = function(a) {
    if (null == a) return null;
    var b = a.indexOf("?");
    var c = -1 < b ? N.substr(a, 0, b) : a;
    for (c = O.replace(c, "\\", "/"); O.endsWith(c,
            "/");) c = N.substr(c, 0, c.length - 1);
    return O.endsWith(c, ".bundle") ? -1 < b ? c + "/library.json" + N.substr(a, b, null) : c + "/library.json" : a
};
td.__resolveRootPath = function(a, b) {
    if (null != a) return a;
    a = b.indexOf("?");
    a = -1 < a ? N.substr(b, 0, a) : b;
    for (a = O.replace(a, "\\", "/"); O.endsWith(a, "/");) {
        if ("/" == a) return a;
        a = N.substr(a, 0, a.length - 1)
    }
    return O.endsWith(a, ".bundle") ? a : Ud.directory(a)
};
td.prototype = {
    __class__: td
};
var Fa = function() {};
g["lime.utils.Assets"] = Fa;
Fa.__name__ = "lime.utils.Assets";
Fa.exists = function(a, b) {
    null ==
        b && (b = "BINARY");
    var c = a.indexOf(":"),
        d = a.substring(0, c);
    a = a.substring(c + 1);
    d = Fa.getLibrary(d);
    return null != d ? d.exists(a, b) : !1
};
Fa.getAsset = function(a, b, c) {
    if (c && Fa.cache.enabled) switch (b) {
        case "FONT":
            var d = Fa.cache.font.h[a];
            if (null != d) return d;
            break;
        case "IMAGE":
            d = Fa.cache.image.h[a];
            if (Fa.isValidImage(d)) return d;
            break;
        case "MUSIC":
        case "SOUND":
            d = Fa.cache.audio.h[a];
            if (Fa.isValidAudio(d)) return d;
            break;
        case "TEMPLATE":
            throw X.thrown("Not sure how to get template: " + a);
        case "BINARY":
        case "TEXT":
            c = !1;
            break;
        default:
            return null
    }
    var f = a.indexOf(":");
    d = a.substring(0, f);
    f = a.substring(f + 1);
    var h = Fa.getLibrary(d);
    if (null != h)
        if (h.exists(f, b)) {
            if (h.isLocal(f, b)) return d = h.getAsset(f, b), c && Fa.cache.enabled && Fa.cache.set(a, b, d), d;
            Ga.error(b + ' asset "' + a + '" exists, but only asynchronously', {
                fileName: "lime/utils/Assets.hx",
                lineNumber: 133,
                className: "lime.utils.Assets",
                methodName: "getAsset"
            })
        } else Ga.error("There is no " + b + ' asset with an ID of "' + a + '"', {
            fileName: "lime/utils/Assets.hx",
            lineNumber: 138,
            className: "lime.utils.Assets",
            methodName: "getAsset"
        });
    else Ga.error(Fa.__libraryNotFound(d), {
        fileName: "lime/utils/Assets.hx",
        lineNumber: 143,
        className: "lime.utils.Assets",
        methodName: "getAsset"
    });
    return null
};
Fa.getBytes = function(a) {
    return Fa.getAsset(a, "BINARY", !1)
};
Fa.getFont = function(a, b) {
    null == b && (b = !0);
    return Fa.getAsset(a, "FONT", b)
};
Fa.getLibrary = function(a) {
    if (null == a || "" == a) a = "default";
    return Fa.libraries.h[a]
};
Fa.getText = function(a) {
    return Fa.getAsset(a, "TEXT", !1)
};
Fa.isLocal = function(a, b, c) {
    null == c && (c = !0);
    if (c && Fa.cache.enabled &&
        Fa.cache.exists(a, b)) return !0;
    var d = a.indexOf(":");
    c = a.substring(0, d);
    a = a.substring(d + 1);
    c = Fa.getLibrary(c);
    return null != c ? c.isLocal(a, b) : !1
};
Fa.isValidAudio = function(a) {
    return null != a
};
Fa.isValidImage = function(a) {
    return null != a ? null != a.buffer : !1
};
Fa.loadLibrary = function(a) {
    var b = new Gd,
        c = Fa.getLibrary(a);
    if (null != c) return c.load();
    c = a;
    var d = null;
    if (Object.prototype.hasOwnProperty.call(Fa.bundlePaths.h, a)) re.loadFromFile(Fa.bundlePaths.h[a]).onComplete(function(c) {
        null == c ? b.error('Cannot load bundle for library "' +
            a + '"') : (c = Wb.fromBundle(c), null == c ? b.error('Cannot open library "' + a + '"') : (Fa.libraries.h[a] = c, c.onChange.add((G = Fa.onChange, l(G, G.dispatch))), b.completeWith(c.load())))
    }).onError(function(c) {
        b.error('There is no asset library with an ID of "' + a + '"')
    });
    else Object.prototype.hasOwnProperty.call(Fa.libraryPaths.h, a) ? (c = Fa.libraryPaths.h[a], d = Ud.directory(c)) : (O.endsWith(c, ".bundle") ? (d = c, c += "/library.json") : d = Ud.directory(c), c = Fa.__cacheBreak(c)), td.loadFromFile(c, d).onComplete(function(c) {
        null == c ?
            b.error('Cannot parse asset manifest for library "' + a + '"') : (c = Wb.fromManifest(c), null == c ? b.error('Cannot open library "' + a + '"') : (Fa.libraries.h[a] = c, c.onChange.add((G = Fa.onChange, l(G, G.dispatch))), b.completeWith(c.load())))
    }).onError(function(c) {
        b.error('There is no asset library with an ID of "' + a + '"')
    });
    return b.future
};
Fa.registerLibrary = function(a, b) {
    if (null == a || "" == a) a = "default";
    if (Object.prototype.hasOwnProperty.call(Fa.libraries.h, a)) {
        if (Fa.libraries.h[a] == b) return;
        Fa.unloadLibrary(a)
    }
    null !=
        b && b.onChange.add(Fa.library_onChange);
    Fa.libraries.h[a] = b
};
Fa.unloadLibrary = function(a) {
    Fa.removeLibrary(a, !0)
};
Fa.removeLibrary = function(a, b) {
    null == b && (b = !0);
    if (null == a || "" == a) a = "default";
    var c = Fa.libraries.h[a];
    null != c && (Fa.cache.clear(a + ":"), c.onChange.remove(Fa.library_onChange), b && c.unload());
    b = Fa.libraries;
    Object.prototype.hasOwnProperty.call(b.h, a) && delete b.h[a]
};
Fa.__cacheBreak = function(a) {
    0 < Fa.cache.version && (a = -1 < a.indexOf("?") ? a + ("&" + Fa.cache.version) : a + ("?" + Fa.cache.version));
    return a
};
Fa.__libraryNotFound = function(a) {
    if (null == a || "" == a) a = "default";
    return null == A.current || null == A.current.__preloader || A.current.__preloader.complete ? 'There is no asset library named "' + a + '"' : 'There is no asset library named "' + a + '", or it is not yet preloaded'
};
Fa.library_onChange = function() {
    Fa.cache.clear();
    Fa.onChange.dispatch()
};
var Ek = function(a, b) {
    this.bytes = a;
    this.offset = b
};
g["lime.utils.BytePointerData"] = Ek;
Ek.__name__ = "lime.utils.BytePointerData";
Ek.prototype = {
    __class__: Ek
};
var Tf = {
        _new: function(a,
            b) {
            return new zb(b)
        },
        decompress: function(a, b) {
            switch (b._hx_index) {
                case 0:
                    return el.decompress(a);
                case 1:
                    return fl.decompress(a);
                case 2:
                    return gl.decompress(a);
                case 3:
                    return hl.decompress(a)
            }
        },
        fromBytes: function(a) {
            return null == a ? null : Tf._new(a.length, a.b.bufferValue)
        },
        fromFile: function(a) {
            return null
        },
        loadFromFile: function(a) {
            return (new Vi).load(a)
        },
        ofString: function(a) {
            a = zb.ofString(a);
            return Tf._new(a.length, a.b.bufferValue)
        }
    },
    Yi = y["lime.utils.CompressionAlgorithm"] = {
        __ename__: "lime.utils.CompressionAlgorithm",
        __constructs__: null,
        DEFLATE: {
            _hx_name: "DEFLATE",
            _hx_index: 0,
            __enum__: "lime.utils.CompressionAlgorithm",
            toString: r
        },
        GZIP: {
            _hx_name: "GZIP",
            _hx_index: 1,
            __enum__: "lime.utils.CompressionAlgorithm",
            toString: r
        },
        LZMA: {
            _hx_name: "LZMA",
            _hx_index: 2,
            __enum__: "lime.utils.CompressionAlgorithm",
            toString: r
        },
        ZLIB: {
            _hx_name: "ZLIB",
            _hx_index: 3,
            __enum__: "lime.utils.CompressionAlgorithm",
            toString: r
        }
    };
Yi.__constructs__ = [Yi.DEFLATE, Yi.GZIP, Yi.LZMA, Yi.ZLIB];
var ih = {
        toArrayBufferView: function(a) {
            return a
        }
    },
    Ga = function() {};
g["lime.utils.Log"] = Ga;
Ga.__name__ = "lime.utils.Log";
Ga.debug = function(a, b) {
    4 <= Ga.level && console.debug("[" + b.className + "] " + H.string(a))
};
Ga.error = function(a, b) {
    if (1 <= Ga.level) {
        a = "[" + b.className + "] ERROR: " + H.string(a);
        if (Ga.throwErrors) throw X.thrown(a);
        console.error(a)
    }
};
Ga.info = function(a, b) {
    3 <= Ga.level && console.info("[" + b.className + "] " + H.string(a))
};
Ga.verbose = function(a, b) {
    5 <= Ga.level && (a = "[" + b.className + "] " + H.string(a), console.log(a))
};
Ga.warn = function(a, b) {
    2 <= Ga.level && console.warn("[" + b.className +
        "] WARNING: " + H.string(a))
};
var Tj = function() {
    this.bytesTotalCache = new Qa;
    this.bytesLoadedCache2 = new Qa;
    this.bytesLoadedCache = new pa;
    this.onProgress = new Pi;
    this.onComplete = new Ac;
    this.bytesTotal = this.bytesLoaded = 0;
    this.libraries = [];
    this.libraryNames = [];
    this.onProgress.add(l(this, this.update))
};
g["lime.utils.Preloader"] = Tj;
Tj.__name__ = "lime.utils.Preloader";
Tj.prototype = {
    addLibrary: function(a) {
        this.libraries.push(a)
    },
    addLibraryName: function(a) {
        -1 == this.libraryNames.indexOf(a) && this.libraryNames.push(a)
    },
    load: function() {
        for (var a = this, b = 0, c = this.libraries; b < c.length;) {
            var d = c[b];
            ++b;
            this.bytesTotal += d.bytesTotal
        }
        this.loadedLibraries = -1;
        this.preloadStarted = !1;
        b = 0;
        for (c = this.libraries; b < c.length;) d = [c[b]], ++b, Ga.verbose("Preloading asset library", {
            fileName: "lime/utils/Preloader.hx",
            lineNumber: 134,
            className: "lime.utils.Preloader",
            methodName: "load"
        }), d[0].load().onProgress(function(b) {
            return function(c, d) {
                a.bytesLoaded = null == a.bytesLoadedCache.h.__keys__[b[0].__id__] ? a.bytesLoaded + c : a.bytesLoaded + (c -
                    a.bytesLoadedCache.h[b[0].__id__]);
                a.bytesLoadedCache.set(b[0], c);
                a.simulateProgress || a.onProgress.dispatch(a.bytesLoaded, a.bytesTotal)
            }
        }(d)).onComplete(function(b) {
            return function(c) {
                a.bytesLoaded = null == a.bytesLoadedCache.h.__keys__[b[0].__id__] ? a.bytesLoaded + b[0].bytesTotal : a.bytesLoaded + ((b[0].bytesTotal | 0) - a.bytesLoadedCache.h[b[0].__id__]);
                a.loadedAssetLibrary()
            }
        }(d)).onError(function() {
            return function(a) {
                Ga.error(a, {
                    fileName: "lime/utils/Preloader.hx",
                    lineNumber: 170,
                    className: "lime.utils.Preloader",
                    methodName: "load"
                })
            }
        }());
        b = 0;
        for (c = this.libraryNames; b < c.length;) ++b, this.bytesTotal += 200;
        this.loadedLibraries++;
        this.preloadStarted = !0;
        this.updateProgress()
    },
    loadedAssetLibrary: function(a) {
        this.loadedLibraries++;
        var b = this.loadedLibraries;
        this.preloadStarted || ++b;
        var c = this.libraries.length + this.libraryNames.length;
        null != a ? Ga.verbose("Loaded asset library: " + a + " [" + b + "/" + c + "]", {
            fileName: "lime/utils/Preloader.hx",
            lineNumber: 197,
            className: "lime.utils.Preloader",
            methodName: "loadedAssetLibrary"
        }) : Ga.verbose("Loaded asset library [" +
            b + "/" + c + "]", {
                fileName: "lime/utils/Preloader.hx",
                lineNumber: 201,
                className: "lime.utils.Preloader",
                methodName: "loadedAssetLibrary"
            });
        this.updateProgress()
    },
    start: function() {
        this.complete || this.simulateProgress || !this.preloadComplete || (this.complete = !0, this.onComplete.dispatch())
    },
    update: function(a, b) {},
    updateProgress: function() {
        var a = this;
        this.simulateProgress || this.onProgress.dispatch(this.bytesLoaded, this.bytesTotal);
        if (this.loadedLibraries == this.libraries.length && !this.initLibraryNames) {
            this.initLibraryNames = !0;
            for (var b = 0, c = this.libraryNames; b < c.length;) {
                var d = [c[b]];
                ++b;
                Ga.verbose("Preloading asset library: " + d[0], {
                    fileName: "lime/utils/Preloader.hx",
                    lineNumber: 239,
                    className: "lime.utils.Preloader",
                    methodName: "updateProgress"
                });
                Fa.loadLibrary(d[0]).onProgress(function(b) {
                    return function(c, d) {
                        0 < d && (Object.prototype.hasOwnProperty.call(a.bytesTotalCache.h, b[0]) || (a.bytesTotalCache.h[b[0]] = d, a.bytesTotal += d - 200), c > d && (c = d), Object.prototype.hasOwnProperty.call(a.bytesLoadedCache2.h, b[0]) ? a.bytesLoaded += c -
                            a.bytesLoadedCache2.h[b[0]] : a.bytesLoaded += c, a.bytesLoadedCache2.h[b[0]] = c, a.simulateProgress || a.onProgress.dispatch(a.bytesLoaded, a.bytesTotal))
                    }
                }(d)).onComplete(function(b) {
                    return function(c) {
                        c = 200;
                        Object.prototype.hasOwnProperty.call(a.bytesTotalCache.h, b[0]) && (c = a.bytesTotalCache.h[b[0]]);
                        Object.prototype.hasOwnProperty.call(a.bytesLoadedCache2.h, b[0]) ? a.bytesLoaded += c - a.bytesLoadedCache2.h[b[0]] : a.bytesLoaded += c;
                        a.loadedAssetLibrary(b[0])
                    }
                }(d)).onError(function() {
                    return function(a) {
                        Ga.error(a, {
                            fileName: "lime/utils/Preloader.hx",
                            lineNumber: 293,
                            className: "lime.utils.Preloader",
                            methodName: "updateProgress"
                        })
                    }
                }())
            }
        }
        this.simulateProgress || this.loadedLibraries != this.libraries.length + this.libraryNames.length || (this.preloadComplete || (this.preloadComplete = !0, Ga.verbose("Preload complete", {
            fileName: "lime/utils/Preloader.hx",
            lineNumber: 306,
            className: "lime.utils.Preloader",
            methodName: "updateProgress"
        })), this.start())
    },
    __class__: Tj
};
var ah = function(a, b) {
    je.call(this, [a, b])
};
g["msignal.Signal2"] = ah;
ah.__name__ = "msignal.Signal2";
ah.__super__ = je;
ah.prototype = v(je.prototype, {
    dispatch: function(a, b) {
        for (var c = this.slots; c.nonEmpty;) c.head.execute(a, b), c = c.tail
    },
    createSlot: function(a, b, c) {
        null == c && (c = 0);
        null == b && (b = !1);
        return new Zi(this, a, b, c)
    },
    __class__: ah
});
var se = function(a, b, c, d) {
    null == d && (d = 0);
    null == c && (c = !1);
    this.signal = a;
    this.set_listener(b);
    this.once = c;
    this.priority = d;
    this.enabled = !0
};
g["msignal.Slot"] = se;
se.__name__ = "msignal.Slot";
se.prototype = {
    remove: function() {
        this.signal.remove(this.listener)
    },
    set_listener: function(a) {
        return this.listener = a
    },
    __class__: se,
    __properties__: {
        set_listener: "set_listener"
    }
};
var gi = function(a, b, c, d) {
    null == d && (d = 0);
    null == c && (c = !1);
    se.call(this, a, b, c, d)
};
g["msignal.Slot0"] = gi;
gi.__name__ = "msignal.Slot0";
gi.__super__ = se;
gi.prototype = v(se.prototype, {
    execute: function() {
        this.enabled && (this.once && this.remove(), this.listener())
    },
    __class__: gi
});
var hi = function(a, b, c, d) {
    null == d && (d = 0);
    null == c && (c = !1);
    se.call(this, a, b, c, d)
};
g["msignal.Slot1"] = hi;
hi.__name__ = "msignal.Slot1";
hi.__super__ = se;
hi.prototype = v(se.prototype, {
    execute: function(a) {
        this.enabled && (this.once && this.remove(), null != this.param && (a = this.param), this.listener(a))
    },
    __class__: hi
});
var Zi = function(a, b, c, d) {
    null == d && (d = 0);
    null == c && (c = !1);
    se.call(this, a, b, c, d)
};
g["msignal.Slot2"] = Zi;
Zi.__name__ = "msignal.Slot2";
Zi.__super__ = se;
Zi.prototype = v(se.prototype, {
    execute: function(a, b) {
        this.enabled && (this.once && this.remove(), null != this.param1 && (a = this.param1), null != this.param2 && (b = this.param2), this.listener(a, b))
    },
    __class__: Zi
});
var Ra = function() {};
g["openfl.Lib"] = Ra;
Ra.__name__ = "openfl.Lib";
Ra.__properties__ = {
    get_current: "get_current"
};
Ra.getTimer = function() {
    return Cc.getTimer()
};
Ra.getURL = function(a, b) {
    Ra.navigateToURL(a, b)
};
Ra.navigateToURL = function(a, b) {
    null == b && (b = "_blank");
    var c = a.url;
    if (w.typeof(a.data) == J.TObject) {
        for (var d = "", f = ya.fields(a.data), h = 0; h < f.length;) {
            var k = f[h];
            ++h;
            0 < d.length && (d += "&");
            var n = encodeURIComponent(k) + "=";
            k = H.string(ya.field(a.data, k));
            d += n + encodeURIComponent(k)
        }
        c = -1 < c.indexOf("?") ? c + ("&" +
            d) : c + ("?" + d)
    }
    Cc.openURL(c, b)
};
Ra.setTimeout = function(a, b, c) {
    var d = ++Ra.__lastTimerID,
        f = Ra.__timers;
    b = Qf.delay(function() {
        Ra.__timers.remove(d);
        if (null != Ra.get_current() && null != Ra.get_current().stage && Ra.get_current().stage.__uncaughtErrorEvents.__enabled) try {
            a.apply(a, null == c ? [] : c)
        } catch (k) {
            Ta.lastError = k;
            var b = X.caught(k).unwrap();
            Ra.get_current().stage.__handleError(b)
        } else a.apply(a, null == c ? [] : c)
    }, b);
    f.h[d] = b;
    return d
};
Ra.get_current = function() {
    null == Dc.current && (Dc.current = new Af);
    return Dc.current
};
var Ye = function() {};
g["openfl._Vector.IVector"] = Ye;
Ye.__name__ = "openfl._Vector.IVector";
Ye.__isInterface__ = !0;
Ye.prototype = {
    __class__: Ye
};
var $i = function(a, b, c) {
    null == b && (b = !1);
    null == a && (a = 0);
    null == c && (c = []);
    this.__array = c;
    0 < a && this.set_length(a);
    this.fixed = b
};
g["openfl._Vector.BoolVector"] = $i;
$i.__name__ = "openfl._Vector.BoolVector";
$i.__interfaces__ = [Ye];
$i.prototype = {
    toJSON: function() {
        return this.__array
    },
    set_length: function(a) {
        if (!this.fixed) {
            var b = this.__array.length;
            0 > a && (a = 0);
            if (a > b)
                for (; b <
                    a;) {
                    var c = b++;
                    this.__array[c] = !1
                } else
                    for (; this.__array.length > a;) this.__array.pop()
        }
        return this.__array.length
    },
    __class__: $i,
    __properties__: {
        set_length: "set_length"
    }
};
var Ie = function(a, b, c, d) {
    null == d && (d = !1);
    null == b && (b = !1);
    null == a && (a = 0);
    if (d) {
        if (this.__array = [], null != c) {
            d = 0;
            for (var f = c.length; d < f;) {
                var h = d++;
                this.__array[h] = c[h]
            }
        }
    } else null == c && (c = []), this.__array = c;
    0 < a && this.set_length(a);
    this.fixed = b
};
g["openfl._Vector.FloatVector"] = Ie;
Ie.__name__ = "openfl._Vector.FloatVector";
Ie.__interfaces__ = [Ye];
Ie.prototype = {
    concat: function(a) {
        return null == a ? new Ie(0, !1, this.__array.slice()) : 0 < a.__array.length ? new Ie(0, !1, this.__array.concat(a.__array)) : new Ie(0, !1, this.__array.slice())
    },
    copy: function() {
        return new Ie(0, this.fixed, this.__array.slice())
    },
    get: function(a) {
        return this.__array[a]
    },
    push: function(a) {
        return this.fixed ? this.__array.length : this.__array.push(a)
    },
    set: function(a, b) {
        return !this.fixed || a < this.__array.length ? this.__array[a] = b : b
    },
    toJSON: function() {
        return this.__array
    },
    get_length: function() {
        return this.__array.length
    },
    set_length: function(a) {
        if (a != this.__array.length && !this.fixed) {
            var b = this.__array.length;
            0 > a && (a = 0);
            if (a > b)
                for (; b < a;) {
                    var c = b++;
                    this.__array[c] = 0
                } else
                    for (; this.__array.length > a;) this.__array.pop()
        }
        return this.__array.length
    },
    __class__: Ie,
    __properties__: {
        set_length: "set_length",
        get_length: "get_length"
    }
};
var aj = function(a, b, c) {
    null == b && (b = !1);
    null == a && (a = 0);
    null == c && (c = []);
    this.__array = c;
    0 < a && this.set_length(a);
    this.fixed = b
};
g["openfl._Vector.FunctionVector"] = aj;
aj.__name__ = "openfl._Vector.FunctionVector";
aj.__interfaces__ = [Ye];
aj.prototype = {
    toJSON: function() {
        return this.__array
    },
    set_length: function(a) {
        if (!this.fixed) {
            var b = this.__array.length;
            0 > a && (a = 0);
            if (a > b)
                for (; b < a;) {
                    var c = b++;
                    this.__array[c] = null
                } else
                    for (; this.__array.length > a;) this.__array.pop()
        }
        return this.__array.length
    },
    __class__: aj,
    __properties__: {
        set_length: "set_length"
    }
};
var Zg = function(a, b, c) {
    null == b && (b = !1);
    null == a && (a = 0);
    null == c && (c = []);
    this.__array = c;
    0 < a && this.set_length(a);
    this.fixed = b
};
g["openfl._Vector.IntVector"] = Zg;
Zg.__name__ =
    "openfl._Vector.IntVector";
Zg.__interfaces__ = [Ye];
Zg.prototype = {
    get: function(a) {
        return this.__array[a]
    },
    iterator: function() {
        return new yf(this.__array)
    },
    push: function(a) {
        return this.fixed ? this.__array.length : this.__array.push(a)
    },
    toJSON: function() {
        return this.__array
    },
    get_length: function() {
        return this.__array.length
    },
    set_length: function(a) {
        if (!this.fixed) {
            var b = this.__array.length;
            0 > a && (a = 0);
            if (a > b)
                for (; b < a;) {
                    var c = b++;
                    this.__array[c] = 0
                } else
                    for (; this.__array.length > a;) this.__array.pop()
        }
        return this.__array.length
    },
    __class__: Zg,
    __properties__: {
        set_length: "set_length",
        get_length: "get_length"
    }
};
var fg = function(a, b, c, d) {
    null == d && (d = !1);
    null == b && (b = !1);
    null == a && (a = 0);
    if (d) {
        if (this.__array = [], null != c) {
            d = 0;
            for (var f = c.length; d < f;) {
                var h = d++;
                this.__array[h] = c[h]
            }
        }
    } else null == c && (c = []), this.__array = c;
    0 < a && this.set_length(a);
    this.fixed = b
};
g["openfl._Vector.ObjectVector"] = fg;
fg.__name__ = "openfl._Vector.ObjectVector";
fg.__interfaces__ = [Ye];
fg.prototype = {
    get: function(a) {
        return this.__array[a]
    },
    indexOf: function(a, b) {
        null ==
            b && (b = 0);
        for (var c = this.__array.length; b < c;) {
            var d = b++;
            if (this.__array[d] == a) return d
        }
        return -1
    },
    insertAt: function(a, b) {
        (!this.fixed || a < this.__array.length) && this.__array.splice(a, 0, b)
    },
    iterator: function() {
        return new yf(this.__array)
    },
    push: function(a) {
        return this.fixed ? this.__array.length : this.__array.push(a)
    },
    removeAt: function(a) {
        return !this.fixed || a < this.__array.length ? this.__array.splice(a, 1)[0] : null
    },
    set: function(a, b) {
        return !this.fixed || a < this.__array.length ? this.__array[a] = b : b
    },
    splice: function(a,
        b) {
        return new fg(0, !1, this.__array.splice(a, b))
    },
    toJSON: function() {
        return this.__array
    },
    unshift: function(a) {
        this.fixed || this.__array.unshift(a)
    },
    get_length: function() {
        return this.__array.length
    },
    set_length: function(a) {
        if (!this.fixed) {
            var b = this.__array.length;
            0 > a && (a = 0);
            if (a > b)
                for (; b < a;) b++, this.__array.push(null);
            else
                for (; this.__array.length > a;) this.__array.pop()
        }
        return this.__array.length
    },
    __class__: fg,
    __properties__: {
        set_length: "set_length",
        get_length: "get_length"
    }
};
var Wg = function() {
    A.call(this);
    null == Dc.application && (Dc.application = this);
    null == Dc.current && (Dc.current = new Af);
    Dc.current.__loaderInfo = te.create(null);
    Dc.current.__loaderInfo.content = Dc.current
};
g["openfl.display.Application"] = Wg;
Wg.__name__ = "openfl.display.Application";
Wg.__super__ = A;
Wg.prototype = v(A.prototype, {
    createWindow: function(a) {
        var b = new vh(this, a);
        this.__windows.push(b);
        this.__windowByID.h[b.id] = b;
        var c = this;
        b.onClose.add(function() {
            c.__onWindowClose(b)
        }, !1, -1E4);
        null == this.__window && (this.__window = b, b.onActivate.add(l(this,
            this.onWindowActivate)), b.onRenderContextLost.add(l(this, this.onRenderContextLost)), b.onRenderContextRestored.add(l(this, this.onRenderContextRestored)), b.onDeactivate.add(l(this, this.onWindowDeactivate)), b.onDropFile.add(l(this, this.onWindowDropFile)), b.onEnter.add(l(this, this.onWindowEnter)), b.onExpose.add(l(this, this.onWindowExpose)), b.onFocusIn.add(l(this, this.onWindowFocusIn)), b.onFocusOut.add(l(this, this.onWindowFocusOut)), b.onFullscreen.add(l(this, this.onWindowFullscreen)), b.onKeyDown.add(l(this,
            this.onKeyDown)), b.onKeyUp.add(l(this, this.onKeyUp)), b.onLeave.add(l(this, this.onWindowLeave)), b.onMinimize.add(l(this, this.onWindowMinimize)), b.onMouseDown.add(l(this, this.onMouseDown)), b.onMouseMove.add(l(this, this.onMouseMove)), b.onMouseMoveRelative.add(l(this, this.onMouseMoveRelative)), b.onMouseUp.add(l(this, this.onMouseUp)), b.onMouseWheel.add(l(this, this.onMouseWheel)), b.onMove.add(l(this, this.onWindowMove)), b.onRender.add(l(this, this.render)), b.onResize.add(l(this, this.onWindowResize)), b.onRestore.add(l(this,
            this.onWindowRestore)), b.onTextEdit.add(l(this, this.onTextEdit)), b.onTextInput.add(l(this, this.onTextInput)), this.onWindowCreate());
        this.onCreateWindow.dispatch(b);
        return b
    },
    exec: function() {
        return A.prototype.exec.call(this)
    },
    __checkForAllWindowsClosed: function() {
        0 < this.__windows.length || A.prototype.__checkForAllWindowsClosed.call(this)
    },
    __onModuleExit: function(a) {
        this.onExit.canceled || (Dc.application == this && (Dc.application = null), A.prototype.__onModuleExit.call(this, a))
    },
    __class__: Wg
});
var qd = function() {
    oa.call(this);
    this.__allowSmoothing = !0;
    this.__pixelRatio = 1;
    this.__tempColorTransform = new Tb;
    this.__worldAlpha = 1
};
g["openfl.display.DisplayObjectRenderer"] = qd;
qd.__name__ = "openfl.display.DisplayObjectRenderer";
qd.__super__ = oa;
qd.prototype = v(oa.prototype, {
    __clear: function() {},
    __getAlpha: function(a) {
        return a * this.__worldAlpha
    },
    __popMaskObject: function(a, b) {},
    __pushMaskObject: function(a, b) {},
    __render: function(a) {},
    __renderEvent: function(a) {
        if (null != a.__customRenderEvent && a.__renderable) {
            a.__customRenderEvent.allowSmoothing =
                this.__allowSmoothing;
            a.__customRenderEvent.objectMatrix.copyFrom(a.__renderTransform);
            a.__customRenderEvent.objectColorTransform.__copyFrom(a.__worldColorTransform);
            a.__customRenderEvent.renderer = this;
            switch (this.__type) {
                case "cairo":
                    a.__customRenderEvent.type = "renderCairo";
                    break;
                case "canvas":
                    a.__customRenderEvent.type = "renderCanvas";
                    break;
                case "dom":
                    a.__customRenderEvent.type = null != a.stage && a.__worldVisible ? "renderDOM" : "clearDOM";
                    break;
                case "opengl":
                    this.__cleared || this.__clear();
                    this.setShader(a.__worldShader);
                    this.__context3D.__flushGL();
                    a.__customRenderEvent.type = "renderOpenGL";
                    break;
                default:
                    return
            }
            this.__setBlendMode(a.__worldBlendMode);
            this.__pushMaskObject(a);
            a.dispatchEvent(a.__customRenderEvent);
            this.__popMaskObject(a);
            "opengl" == this.__type && this.setViewport()
        }
    },
    __resize: function(a, b) {},
    __setBlendMode: function(a) {},
    __shouldCacheHardware: function(a, b) {
        if (null == a) return null;
        switch (a.__drawableType) {
            case 4:
            case 5:
                if (1 == b) return !0;
                b = this.__shouldCacheHardware_DisplayObject(a, b);
                if (1 == b) return !0;
                if (null !=
                    a.__children) {
                    var c = 0;
                    for (a = a.__children; c < a.length;) {
                        var d = a[c];
                        ++c;
                        b = this.__shouldCacheHardware_DisplayObject(d, b);
                        if (1 == b) return !0
                    }
                }
                return b;
            case 7:
                return 1 == b ? !0 : !1;
            case 9:
                return !0;
            default:
                return this.__shouldCacheHardware_DisplayObject(a, b)
        }
    },
    __shouldCacheHardware_DisplayObject: function(a, b) {
        return 1 == b || null != a.__filters ? !0 : 0 == b || null != a.__graphics && !Yb.isCompatible(a.__graphics) ? !1 : null
    },
    __updateCacheBitmap: function(a, b) {
        if (null == a) return !1;
        switch (a.__drawableType) {
            case 2:
                var c = a;
                if (null ==
                    c.__bitmapData || null == c.__filters && "opengl" == this.__type && null == c.__cacheBitmap) return !1;
                b = null != c.__bitmapData.image && c.__bitmapData.image.version != c.__imageVersion;
                break;
            case 7:
                var d = a;
                if (null == d.__filters && "opengl" == this.__type && null == d.__cacheBitmap && !d.__domRender) return !1;
                b && (d.__renderDirty = !0);
                b = b || d.__dirty;
                break;
            case 9:
                if (null == a.__filters && "opengl" == this.__type && null == a.__cacheBitmap) return !1
        }
        if (a.__isCacheBitmapRender) return !1;
        d = Tb.__pool.get();
        d.__copyFrom(a.__worldColorTransform);
        null !=
            this.__worldColorTransform && d.__combine(this.__worldColorTransform);
        var f = !1;
        if (a.get_cacheAsBitmap() || "opengl" != this.__type && !d.__isDefault(!0)) {
            f = null;
            var h = (b = null == a.__cacheBitmap || a.__renderDirty && (b || null != a.__children && 0 < a.__children.length) || a.opaqueBackground != a.__cacheBitmapBackground) || null != a.__graphics && a.__graphics.__softwareDirty || !a.__cacheBitmapColorTransform.__equals(d, !0),
                k = b || null != a.__graphics && a.__graphics.__hardwareDirty,
                n = this.__type;
            if (h || k) "opengl" == n && 0 == this.__shouldCacheHardware(a,
                null) && (n = "canvas"), !h || "canvas" != n && "cairo" != n || (b = !0), k && "opengl" == n && (b = !0);
            h = b || !a.__cacheBitmap.__worldTransform.equals(a.__worldTransform);
            c = null != a.__filters;
            if ("dom" == this.__type && !c) return !1;
            if (c && !b)
                for (var p = 0, g = a.__filters; p < g.length;) {
                    var q = g[p];
                    ++p;
                    if (q.__renderDirty) {
                        b = !0;
                        break
                    }
                }
            null == a.__cacheBitmapMatrix && (a.__cacheBitmapMatrix = new ua);
            p = null != a.__cacheAsBitmapMatrix ? a.__cacheAsBitmapMatrix : a.__renderTransform;
            b || p.a == a.__cacheBitmapMatrix.a && p.b == a.__cacheBitmapMatrix.b && p.c == a.__cacheBitmapMatrix.c &&
                p.d == a.__cacheBitmapMatrix.d || (b = !0);
            !b && "opengl" != this.__type && null != a.__cacheBitmapData && null != a.__cacheBitmapData.image && a.__cacheBitmapData.image.version < a.__cacheBitmapData.__textureVersion && (b = !0);
            if (!b)
                for (k = a; null != k;) {
                    if (null != k.get_scrollRect()) {
                        h = !0;
                        break
                    }
                    k = k.parent
                }
            a.__cacheBitmapMatrix.copyFrom(p);
            a.__cacheBitmapMatrix.tx = 0;
            var m = k = a.__cacheBitmapMatrix.ty = 0,
                u = 0,
                r = 0,
                l = q = 0;
            g = this.__pixelRatio;
            if (h || b) f = na.__pool.get(), a.__getFilterBounds(f, a.__cacheBitmapMatrix), u = 0 < f.width ? Math.ceil((f.width +
                1) * g) : 0, r = 0 < f.height ? Math.ceil((f.height + 1) * g) : 0, q = 0 < f.x ? Math.ceil(f.x) : Math.floor(f.x), l = 0 < f.y ? Math.ceil(f.y) : Math.floor(f.y), null != a.__cacheBitmapData ? u > a.__cacheBitmapData.width || r > a.__cacheBitmapData.height ? (k = Math.ceil(Math.max(1.25 * u, a.__cacheBitmapData.width)), m = Math.ceil(Math.max(1.25 * r, a.__cacheBitmapData.height)), b = !0) : (k = a.__cacheBitmapData.width, m = a.__cacheBitmapData.height) : (k = u, m = r);
            if (b)
                if (h = !0, a.__cacheBitmapBackground = a.opaqueBackground, .5 <= u && .5 <= r) {
                    var D = null != a.opaqueBackground &&
                        (k != u || m != r),
                        x = null != a.opaqueBackground ? -16777216 | a.opaqueBackground : 0,
                        w = D ? 0 : x,
                        z = "opengl" == this.__type;
                    null == a.__cacheBitmapData || k > a.__cacheBitmapData.width || m > a.__cacheBitmapData.height ? (a.__cacheBitmapData = new Fb(k, m, !0, w), null == a.__cacheBitmap && (a.__cacheBitmap = new Nd), a.__cacheBitmap.__bitmapData = a.__cacheBitmapData, a.__cacheBitmapRenderer = null) : a.__cacheBitmapData.__fillRect(a.__cacheBitmapData.rect, w, z);
                    D && (f.setTo(0, 0, u, r), a.__cacheBitmapData.__fillRect(f, x, z))
                } else return Tb.__pool.release(d),
                    a.__cacheBitmap = null, a.__cacheBitmapData = null, a.__cacheBitmapData2 = null, a.__cacheBitmapData3 = null, a.__cacheBitmapRenderer = null, 7 == a.__drawableType && (d = a, null != d.__cacheBitmap && (d.__cacheBitmap.__renderTransform.tx -= d.__offsetX * g, d.__cacheBitmap.__renderTransform.ty -= d.__offsetY * g)), !0;
            else a.__cacheBitmapData = a.__cacheBitmap.get_bitmapData(), a.__cacheBitmapData2 = null, a.__cacheBitmapData3 = null;
            if (h || b) a.__cacheBitmap.__worldTransform.copyFrom(a.__worldTransform), p == a.__renderTransform ? (a.__cacheBitmap.__renderTransform.identity(),
                a.__cacheBitmap.__renderTransform.scale(1 / g, 1 / g), a.__cacheBitmap.__renderTransform.tx = a.__renderTransform.tx + q, a.__cacheBitmap.__renderTransform.ty = a.__renderTransform.ty + l) : (a.__cacheBitmap.__renderTransform.copyFrom(a.__cacheBitmapMatrix), a.__cacheBitmap.__renderTransform.invert(), a.__cacheBitmap.__renderTransform.concat(a.__renderTransform), a.__cacheBitmap.__renderTransform.a *= 1 / g, a.__cacheBitmap.__renderTransform.d *= 1 / g, a.__cacheBitmap.__renderTransform.tx += q, a.__cacheBitmap.__renderTransform.ty +=
                l);
            a.__cacheBitmap.smoothing = this.__allowSmoothing;
            a.__cacheBitmap.__renderable = a.__renderable;
            a.__cacheBitmap.__worldAlpha = a.__worldAlpha;
            a.__cacheBitmap.__worldBlendMode = a.__worldBlendMode;
            a.__cacheBitmap.__worldShader = a.__worldShader;
            a.__cacheBitmap.set_mask(a.__mask);
            if (b) {
                if (null == a.__cacheBitmapRenderer || n != a.__cacheBitmapRenderer.__type) "opengl" == n ? a.__cacheBitmapRenderer = new db(va.__cast(this, db).__context3D, a.__cacheBitmapData) : (null == a.__cacheBitmapData.image && (a.__cacheBitmapData = new Fb(k,
                    m, !0, null != a.opaqueBackground ? -16777216 | a.opaqueBackground : 0), a.__cacheBitmap.__bitmapData = a.__cacheBitmapData), Ka.convertToCanvas(a.__cacheBitmapData.image), a.__cacheBitmapRenderer = new Ue(a.__cacheBitmapData.image.buffer.__srcContext)), a.__cacheBitmapRenderer.__worldTransform = new ua, a.__cacheBitmapRenderer.__worldColorTransform = new Tb;
                null == a.__cacheBitmapColorTransform && (a.__cacheBitmapColorTransform = new Tb);
                a.__cacheBitmapRenderer.__stage = a.stage;
                a.__cacheBitmapRenderer.__allowSmoothing = this.__allowSmoothing;
                a.__cacheBitmapRenderer.__setBlendMode(10);
                a.__cacheBitmapRenderer.__worldAlpha = 1 / a.__worldAlpha;
                a.__cacheBitmapRenderer.__worldTransform.copyFrom(a.__renderTransform);
                a.__cacheBitmapRenderer.__worldTransform.invert();
                a.__cacheBitmapRenderer.__worldTransform.concat(a.__cacheBitmapMatrix);
                a.__cacheBitmapRenderer.__worldTransform.tx -= q;
                a.__cacheBitmapRenderer.__worldTransform.ty -= l;
                a.__cacheBitmapRenderer.__worldTransform.scale(g, g);
                a.__cacheBitmapRenderer.__pixelRatio = g;
                a.__cacheBitmapRenderer.__worldColorTransform.__copyFrom(d);
                a.__cacheBitmapRenderer.__worldColorTransform.__invert();
                a.__isCacheBitmapRender = !0;
                if ("opengl" == a.__cacheBitmapRenderer.__type) {
                    D = a.__cacheBitmapRenderer;
                    x = D.__context3D;
                    w = x.__state.renderToTexture;
                    z = x.__state.renderToTextureDepthStencil;
                    var J = x.__state.renderToTextureAntiAlias,
                        y = x.__state.renderToTextureSurfaceSelector,
                        C = this.__blendMode;
                    this.__suspendClipAndMask();
                    D.__copyShader(this);
                    a.__cacheBitmapData.__setUVRect(x, 0, 0, u, r);
                    D.__setRenderTarget(a.__cacheBitmapData);
                    null != a.__cacheBitmapData.image &&
                        (a.__cacheBitmapData.__textureVersion = a.__cacheBitmapData.image.version + 1);
                    a.__cacheBitmapData.__drawGL(a, D);
                    if (c) {
                        var t = !1;
                        p = 0;
                        for (g = a.__filters; p < g.length;) q = g[p], ++p, q.__preserveObject && (t = !0);
                        c = a.__cacheBitmapData;
                        l = null;
                        null == a.__cacheBitmapData2 || k > a.__cacheBitmapData2.width || m > a.__cacheBitmapData2.height ? a.__cacheBitmapData2 = new Fb(k, m, !0, 0) : (a.__cacheBitmapData2.fillRect(a.__cacheBitmapData2.rect, 0), null != a.__cacheBitmapData2.image && (a.__cacheBitmapData2.__textureVersion = a.__cacheBitmapData2.image.version +
                            1));
                        a.__cacheBitmapData2.__setUVRect(x, 0, 0, u, r);
                        n = a.__cacheBitmapData2;
                        t && (null == a.__cacheBitmapData3 || k > a.__cacheBitmapData3.width || m > a.__cacheBitmapData3.height ? a.__cacheBitmapData3 = new Fb(k, m, !0, 0) : (a.__cacheBitmapData3.fillRect(a.__cacheBitmapData3.rect, 0), null != a.__cacheBitmapData3.image && (a.__cacheBitmapData3.__textureVersion = a.__cacheBitmapData3.image.version + 1)), a.__cacheBitmapData3.__setUVRect(x, 0, 0, u, r), l = a.__cacheBitmapData3);
                        D.__setBlendMode(10);
                        D.__worldAlpha = 1;
                        D.__worldTransform.identity();
                        D.__worldColorTransform.__identity();
                        p = 0;
                        for (g = a.__filters; p < g.length;) {
                            q = g[p];
                            ++p;
                            q.__preserveObject && (D.__setRenderTarget(l), D.__renderFilterPass(c, D.__defaultDisplayShader, q.__smooth));
                            m = 0;
                            for (u = q.__numShaderPasses; m < u;) k = m++, k = q.__initShader(D, k, q.__preserveObject ? l : null), D.__setBlendMode(q.__shaderBlendMode), D.__setRenderTarget(n), D.__renderFilterPass(c, k, q.__smooth), k = c, c = n, n = k;
                            q.__renderDirty = !1
                        }
                        a.__cacheBitmap.__bitmapData = c
                    }
                    this.__blendMode = 10;
                    this.__setBlendMode(C);
                    this.__copyShader(D);
                    null != w ? x.setRenderToTexture(w, z, J, y) : x.setRenderToBackBuffer();
                    this.__resumeClipAndMask(D);
                    this.setViewport();
                    a.__cacheBitmapColorTransform.__copyFrom(d)
                } else {
                    a.__cacheBitmapData.__drawCanvas(a, a.__cacheBitmapRenderer);
                    if (c) {
                        t = u = !1;
                        p = 0;
                        for (g = a.__filters; p < g.length;) q = g[p], ++p, q.__needSecondBitmapData && (u = !0), q.__preserveObject && (t = !0);
                        c = a.__cacheBitmapData;
                        l = null;
                        u ? (null == a.__cacheBitmapData2 || null == a.__cacheBitmapData2.image || k > a.__cacheBitmapData2.width || m > a.__cacheBitmapData2.height ? a.__cacheBitmapData2 =
                            new Fb(k, m, !0, 0) : a.__cacheBitmapData2.fillRect(a.__cacheBitmapData2.rect, 0), n = a.__cacheBitmapData2) : n = c;
                        t && (null == a.__cacheBitmapData3 || null == a.__cacheBitmapData3.image || k > a.__cacheBitmapData3.width || m > a.__cacheBitmapData3.height ? a.__cacheBitmapData3 = new Fb(k, m, !0, 0) : a.__cacheBitmapData3.fillRect(a.__cacheBitmapData3.rect, 0), l = a.__cacheBitmapData3);
                        null == a.__tempPoint && (a.__tempPoint = new I);
                        m = a.__tempPoint;
                        p = 0;
                        for (g = a.__filters; p < g.length;) q = g[p], ++p, q.__preserveObject && l.copyPixels(c, c.rect, m), k =
                            q.__applyFilter(n, c, c.rect, m), q.__preserveObject && k.draw(l, null, null != a.__objectTransform ? a.__objectTransform.__colorTransform : null), q.__renderDirty = !1, u && k == n && (k = c, c = n, n = k);
                        a.__cacheBitmapData != c && (k = a.__cacheBitmapData, a.__cacheBitmapData = c, a.__cacheBitmapData2 = k, a.__cacheBitmap.__bitmapData = a.__cacheBitmapData, a.__cacheBitmapRenderer = null);
                        a.__cacheBitmap.__imageVersion = a.__cacheBitmapData.__textureVersion
                    }
                    a.__cacheBitmapColorTransform.__copyFrom(d);
                    a.__cacheBitmapColorTransform.__isDefault(!0) ||
                        (a.__cacheBitmapColorTransform.alphaMultiplier = 1, a.__cacheBitmapData.colorTransform(a.__cacheBitmapData.rect, a.__cacheBitmapColorTransform))
                }
                a.__isCacheBitmapRender = !1
            }(h || b) && na.__pool.release(f);
            f = h
        } else null != a.__cacheBitmap && ("dom" == this.__type && this.__renderDrawableClear(a.__cacheBitmap), a.__cacheBitmap = null, a.__cacheBitmapData = null, a.__cacheBitmapData2 = null, a.__cacheBitmapData3 = null, a.__cacheBitmapColorTransform = null, a.__cacheBitmapRenderer = null, f = !0);
        Tb.__pool.release(d);
        f && 7 == a.__drawableType &&
            (d = a, null != d.__cacheBitmap && (d.__cacheBitmap.__renderTransform.tx -= d.__offsetX, d.__cacheBitmap.__renderTransform.ty -= d.__offsetY));
        return f
    },
    __class__: qd
});
var bj = function(a) {
    qd.call(this)
};
g["openfl.display.CairoRenderer"] = bj;
bj.__name__ = "openfl.display.CairoRenderer";
bj.__super__ = qd;
bj.prototype = v(qd.prototype, {
    applyMatrix: function(a, b) {
        null == b && (b = this.cairo);
        this.__matrix.copyFrom(a);
        this.cairo == b && null != this.__worldTransform && this.__matrix.concat(this.__worldTransform);
        this.__matrix3[0] = this.__matrix.a;
        this.__matrix3[1] = this.__matrix.b;
        this.__matrix3[3] = this.__matrix.c;
        this.__matrix3[4] = this.__matrix.d;
        this.__roundPixels ? (this.__matrix3[6] = Math.round(this.__matrix.tx), this.__matrix3[7] = Math.round(this.__matrix.ty)) : (this.__matrix3[6] = this.__matrix.tx, this.__matrix3[7] = this.__matrix.ty);
        b.set_matrix(this.__matrix3)
    },
    __clear: function() {
        if (null != this.cairo && (this.cairo.identityMatrix(), null != this.__stage && this.__stage.__clearBeforeRender)) {
            var a = this.__blendMode;
            this.__setBlendMode(10);
            this.cairo.setSourceRGB(this.__stage.__colorSplit[0],
                this.__stage.__colorSplit[1], this.__stage.__colorSplit[2]);
            this.cairo.paint();
            this.__setBlendMode(a)
        }
    },
    __popMask: function() {
        this.cairo.restore()
    },
    __popMaskObject: function(a, b) {
        null == b && (b = !0);
        a.__isCacheBitmapRender || null == a.__mask || this.__popMask();
        b && null != a.__scrollRect && this.__popMaskRect()
    },
    __popMaskRect: function() {
        this.cairo.restore()
    },
    __pushMask: function(a) {
        this.cairo.save();
        this.applyMatrix(a.__renderTransform, this.cairo);
        this.cairo.newPath();
        this.__renderDrawableMask(a);
        this.cairo.clip()
    },
    __pushMaskObject: function(a, b) {
        null == b && (b = !0);
        b && null != a.__scrollRect && this.__pushMaskRect(a.__scrollRect, a.__renderTransform);
        a.__isCacheBitmapRender || null == a.__mask || this.__pushMask(a.__mask)
    },
    __pushMaskRect: function(a, b) {
        this.cairo.save();
        this.applyMatrix(b, this.cairo);
        this.cairo.newPath();
        this.cairo.rectangle(a.x, a.y, a.width, a.height);
        this.cairo.clip()
    },
    __render: function(a) {
        null != this.cairo && this.__renderDrawable(a)
    },
    __renderDrawable: function(a) {
        if (null != a) switch (a.__drawableType) {
            case 6:
                !a.__renderable ||
                    0 >= a.__worldAlpha || null == a.__currentState || (this.__pushMaskObject(a), this.__renderDrawable(a.__currentState), this.__popMaskObject(a), this.__renderEvent(a))
        }
    },
    __renderDrawableMask: function(a) {
        if (null != a) switch (a.__drawableType) {
            case 2:
                this.cairo.rectangle(0, 0, a.get_width(), a.get_height());
                break;
            case 6:
                this.__renderDrawableMask(a.__currentState)
        }
    },
    __setBlendMode: function(a) {
        null != this.__overrideBlendMode && (a = this.__overrideBlendMode);
        this.__blendMode != a && (this.__blendMode = a, this.__setBlendModeCairo(this.cairo,
            a))
    },
    __setBlendModeCairo: function(a, b) {
        switch (b) {
            case 0:
                a.setOperator(12);
                break;
            case 2:
                a.setOperator(17);
                break;
            case 3:
                a.setOperator(23);
                break;
            case 5:
                a.setOperator(21);
                break;
            case 7:
                a.setOperator(2);
                break;
            case 8:
                a.setOperator(18);
                break;
            case 9:
                a.setOperator(14);
                break;
            case 11:
                a.setOperator(16);
                break;
            case 12:
                a.setOperator(15);
                break;
            default:
                a.setOperator(2)
        }
    },
    __class__: bj
});
var Ue = function(a) {
    qd.call(this);
    this.context = a;
    this.__tempMatrix = new ua;
    this.__type = "canvas"
};
g["openfl.display.CanvasRenderer"] =
    Ue;
Ue.__name__ = "openfl.display.CanvasRenderer";
Ue.__super__ = qd;
Ue.prototype = v(qd.prototype, {
    applySmoothing: function(a, b) {
        a.imageSmoothingEnabled = b
    },
    setTransform: function(a, b) {
        null == b ? b = this.context : this.context == b && null != this.__worldTransform && (this.__tempMatrix.copyFrom(a), this.__tempMatrix.concat(this.__worldTransform), a = this.__tempMatrix);
        this.__roundPixels ? b.setTransform(a.a, a.b, a.c, a.d, a.tx | 0, a.ty | 0) : b.setTransform(a.a, a.b, a.c, a.d, a.tx, a.ty)
    },
    __clear: function() {
        if (null != this.__stage) {
            var a = this.__blendMode;
            this.__blendMode = null;
            this.__setBlendMode(10);
            this.context.setTransform(1, 0, 0, 1, 0, 0);
            this.context.globalAlpha = 1;
            !this.__stage.__transparent && this.__stage.__clearBeforeRender ? (this.context.fillStyle = this.__stage.__colorString, this.context.fillRect(0, 0, this.__stage.stageWidth * this.__stage.window.__scale, this.__stage.stageHeight * this.__stage.window.__scale)) : this.__stage.__transparent && this.__stage.__clearBeforeRender && this.context.clearRect(0, 0, this.__stage.stageWidth * this.__stage.window.__scale, this.__stage.stageHeight *
                this.__stage.window.__scale);
            this.__setBlendMode(a)
        }
    },
    __popMask: function() {
        this.context.restore()
    },
    __popMaskObject: function(a, b) {
        null == b && (b = !0);
        a.__isCacheBitmapRender || null == a.__mask || this.__popMask();
        b && null != a.__scrollRect && this.__popMaskRect()
    },
    __popMaskRect: function() {
        this.context.restore()
    },
    __pushMask: function(a) {
        this.context.save();
        this.setTransform(a.__renderTransform, this.context);
        this.context.beginPath();
        this.__renderDrawableMask(a);
        this.context.closePath();
        this.context.clip()
    },
    __pushMaskObject: function(a,
        b) {
        null == b && (b = !0);
        b && null != a.__scrollRect && this.__pushMaskRect(a.__scrollRect, a.__renderTransform);
        a.__isCacheBitmapRender || null == a.__mask || this.__pushMask(a.__mask)
    },
    __pushMaskRect: function(a, b) {
        this.context.save();
        this.setTransform(b, this.context);
        this.context.beginPath();
        this.context.rect(a.x, a.y, a.width, a.height);
        this.context.clip()
    },
    __render: function(a) {
        this.__renderDrawable(a)
    },
    __renderDrawable: function(a) {
        if (null != a) switch (a.__drawableType) {
            case 0:
                cj.renderDrawable(a, this);
                break;
            case 2:
                wh.renderDrawable(a,
                    this);
                break;
            case 3:
                Uf.renderDrawable(a, this);
                break;
            case 4:
            case 5:
                dj.renderDrawable(a, this);
                break;
            case 6:
                ej.renderDrawable(a, this);
                break;
            case 7:
                Q.renderDrawable(a, this);
                break;
            case 8:
                Vf.renderDrawable(a, this);
                break;
            case 9:
                Ze.renderDrawable(a, this)
        }
    },
    __renderDrawableMask: function(a) {
        if (null != a) switch (a.__drawableType) {
            case 0:
                cj.renderDrawableMask(a, this);
                break;
            case 2:
                wh.renderDrawableMask(a, this);
                break;
            case 3:
                Uf.renderDrawableMask(a, this);
                break;
            case 4:
            case 5:
                dj.renderDrawableMask(a, this);
                break;
            case 6:
                ej.renderDrawableMask(a,
                    this);
                break;
            case 7:
                Q.renderDrawableMask(a, this);
                break;
            case 8:
                Vf.renderDrawableMask(a, this);
                break;
            case 9:
                Ze.renderDrawableMask(a, this)
        }
    },
    __setBlendMode: function(a) {
        null != this.__overrideBlendMode && (a = this.__overrideBlendMode);
        this.__blendMode != a && (this.__blendMode = a, this.__setBlendModeContext(this.context, a))
    },
    __setBlendModeContext: function(a, b) {
        switch (b) {
            case 0:
                a.globalCompositeOperation = "lighter";
                break;
            case 2:
                a.globalCompositeOperation = "darken";
                break;
            case 3:
                a.globalCompositeOperation = "difference";
                break;
            case 5:
                a.globalCompositeOperation = "hard-light";
                break;
            case 8:
                a.globalCompositeOperation = "lighten";
                break;
            case 9:
                a.globalCompositeOperation = "multiply";
                break;
            case 11:
                a.globalCompositeOperation = "overlay";
                break;
            case 12:
                a.globalCompositeOperation = "screen";
                break;
            default:
                a.globalCompositeOperation = "source-over"
        }
    },
    __class__: Ue
});
var Ll = {
        toString: function(a) {
            switch (a) {
                case 0:
                    return "none";
                case 1:
                    return "round";
                case 2:
                    return "square";
                default:
                    return null
            }
        }
    },
    fj = function(a) {
        S.call(this);
        this.__drawableType = 10;
        this.__element = a
    };
g["openfl.display.DOMElement"] = fj;
fj.__name__ = "openfl.display.DOMElement";
fj.__super__ = S;
fj.prototype = v(S.prototype, {
    __class__: fj
});
var xh = function(a) {
    qd.call(this);
    this.element = a;
    S.__supportDOM = !0;
    a = window.getComputedStyle(document.documentElement, "");
    a = (Array.prototype.slice.call(a).join("").match(/-(moz|webkit|ms)-/) || "" === a.OLink && ["", "o"])[1];
    "WebKit|Moz|MS|O".match(new RegExp("(" + a + ")", "i"));
    a[0].toUpperCase();
    a.substr(1);
    this.__vendorPrefix = a;
    this.__transformProperty = "webkit" ==
        a ? "-webkit-transform" : "transform";
    this.__transformOriginProperty = "webkit" == a ? "-webkit-transform-origin" : "transform-origin";
    this.__clipRects = [];
    this.__z = this.__numClipRects = 0;
    this.__type = "dom";
    this.__canvasRenderer = new Ue(null);
    this.__canvasRenderer.__isDOM = !0
};
g["openfl.display.DOMRenderer"] = xh;
xh.__name__ = "openfl.display.DOMRenderer";
xh.__super__ = qd;
xh.prototype = v(qd.prototype, {
    __applyStyle: function(a, b, c, d) {
        var f = a.__style;
        if (b && a.__renderTransformChanged) {
            b = a.__renderTransform;
            var h = this.__roundPixels;
            null == h && (h = !1);
            f.setProperty(this.__transformProperty, h ? "matrix3d(" + b.a + ", " + b.b + ", 0, 0, " + b.c + ", " + b.d + ", 0, 0, 0, 0, 1, 0, " + (b.tx | 0) + ", " + (b.ty | 0) + ", 0, 1)" : "matrix3d(" + b.a + ", " + b.b + ", 0, 0, " + b.c + ", " + b.d + ", 0, 0, 0, 0, 1, 0, " + b.tx + ", " + b.ty + ", 0, 1)", null)
        }
        a.__worldZ != ++this.__z && (a.__worldZ = this.__z, f.setProperty("z-index", null == a.__worldZ ? "null" : "" + a.__worldZ, null));
        c && a.__worldAlphaChanged && (1 > a.__worldAlpha ? f.setProperty("opacity", null == a.__worldAlpha ? "null" : "" + a.__worldAlpha, null) : f.removeProperty("opacity"));
        d && a.__worldClipChanged && (null == a.__worldClip ? f.removeProperty("clip") : (a = a.__worldClip, f.setProperty("clip", "rect(" + a.y + "px, " + a.get_right() + "px, " + a.get_bottom() + "px, " + a.x + "px)", null)))
    },
    __initializeElement: function(a, b) {
        var c = a.__style = b.style;
        c.setProperty("position", "absolute", null);
        c.setProperty("top", "0", null);
        c.setProperty("left", "0", null);
        c.setProperty(this.__transformOriginProperty, "0 0 0", null);
        this.element.appendChild(b);
        a.__worldAlphaChanged = !0;
        a.__renderTransformChanged = !0;
        a.__worldVisibleChanged = !0;
        a.__worldClipChanged = !0;
        a.__worldClip = null;
        a.__worldZ = -1
    },
    __popMask: function() {
        this.__popMaskRect()
    },
    __popMaskObject: function(a, b) {
        null == b && (b = !0);
        null != a.__mask && this.__popMask();
        b && null != a.__scrollRect && this.__popMaskRect()
    },
    __popMaskRect: function() {
        0 < this.__numClipRects && (this.__numClipRects--, this.__currentClipRect = 0 < this.__numClipRects ? this.__clipRects[this.__numClipRects - 1] : null)
    },
    __pushMask: function(a) {
        this.__pushMaskRect(a.getBounds(a), a.__renderTransform)
    },
    __pushMaskObject: function(a,
        b) {
        null == b && (b = !0);
        b && null != a.__scrollRect && this.__pushMaskRect(a.__scrollRect, a.__renderTransform);
        null != a.__mask && this.__pushMask(a.__mask)
    },
    __pushMaskRect: function(a, b) {
        this.__numClipRects == this.__clipRects.length && (this.__clipRects[this.__numClipRects] = new na);
        var c = this.__clipRects[this.__numClipRects];
        a.__transform(c, b);
        0 < this.__numClipRects && (a = this.__clipRects[this.__numClipRects - 1], c.__contract(a.x, a.y, a.width, a.height));
        0 > c.height && (c.height = 0);
        0 > c.width && (c.width = 0);
        this.__currentClipRect =
            c;
        this.__numClipRects++
    },
    __render: function(a) {
        this.element.style.background = this.__stage.__transparent ? "none" : this.__stage.__colorString;
        this.__z = 1;
        this.__renderDrawable(a)
    },
    __renderDrawable: function(a) {
        if (null != a) switch (a.__drawableType) {
            case 2:
                Zb.renderDrawable(a, this);
                break;
            case 3:
                rd.renderDrawable(a, this);
                break;
            case 4:
            case 5:
                gj.renderDrawable(a, this);
                break;
            case 6:
                hj.renderDrawable(a, this);
                break;
            case 7:
                uf.renderDrawable(a, this);
                break;
            case 8:
                $e.renderDrawable(a, this);
                break;
            case 9:
                vf.renderDrawable(a,
                    this);
                break;
            case 10:
                null != a.stage && a.__worldVisible && a.__renderable ? (a.__active || (this.__initializeElement(a, a.__element), a.__active = !0), this.__updateClip(a), this.__applyStyle(a, !0, !0, !0)) : a.__active && (this.element.removeChild(a.__element), a.__active = !1), rd.renderDrawable(a, this)
        }
    },
    __renderDrawableClear: function(a) {
        if (null != a) switch (a.__drawableType) {
            case 2:
                Zb.renderDrawableClear(a, this);
                break;
            case 3:
                rd.renderDrawableClear(a, this);
                break;
            case 4:
            case 5:
                gj.renderDrawableClear(a, this);
                break;
            case 6:
                hj.renderDrawableClear(a,
                    this);
                break;
            case 7:
                uf.renderDrawableClear(a, this);
                break;
            case 8:
                $e.renderDrawableClear(a, this);
                break;
            case 9:
                vf.renderDrawableClear(a, this)
        }
    },
    __setBlendMode: function(a) {
        null != this.__overrideBlendMode && (a = this.__overrideBlendMode);
        this.__blendMode != a && (this.__blendMode = a)
    },
    __updateClip: function(a) {
        if (null == this.__currentClipRect) a.__worldClipChanged = null != a.__worldClip, a.__worldClip = null;
        else {
            null == a.__worldClip && (a.__worldClip = new na);
            var b = na.__pool.get(),
                c = ua.__pool.get();
            c.copyFrom(a.__renderTransform);
            c.invert();
            this.__currentClipRect.__transform(b, c);
            b.equals(a.__worldClip) ? a.__worldClipChanged = !1 : (a.__worldClip.copyFrom(b), a.__worldClipChanged = !0);
            na.__pool.release(b);
            ua.__pool.release(c)
        }
    },
    __class__: xh
});
var Dd = function(a) {
    this.byteCode = a;
    this.precisionHint = 1;
    this.__glSourceDirty = !0;
    this.__numPasses = 1;
    this.__data = zl._new(a)
};
g["openfl.display.Shader"] = Dd;
Dd.__name__ = "openfl.display.Shader";
Dd.prototype = {
    __clearUseArray: function() {
        for (var a = 0, b = this.__paramBool; a < b.length;) {
            var c = b[a];
            ++a;
            c.__useArray = !1
        }
        a = 0;
        for (b = this.__paramFloat; a < b.length;) c = b[a], ++a, c.__useArray = !1;
        a = 0;
        for (b = this.__paramInt; a < b.length;) c = b[a], ++a, c.__useArray = !1
    },
    __createGLShader: function(a, b) {
        var c = this.__context.gl,
            d = c.createShader(b);
        c.shaderSource(d, a);
        c.compileShader(d);
        var f = c.getShaderInfoLog(d),
            h = null != f && "" != O.trim(f),
            k = c.getShaderParameter(d, c.COMPILE_STATUS);
        if (h || 0 == k) b = (0 == k ? "Error" : "Info") + (b == c.VERTEX_SHADER ? " compiling vertex shader" : " compiling fragment shader"), b = b + ("\n" + f) + ("\n" + a), 0 == k ? Ga.error(b, {
            fileName: "openfl/display/Shader.hx",
            lineNumber: 337,
            className: "openfl.display.Shader",
            methodName: "__createGLShader"
        }) : h && Ga.debug(b, {
            fileName: "openfl/display/Shader.hx",
            lineNumber: 338,
            className: "openfl.display.Shader",
            methodName: "__createGLShader"
        });
        return d
    },
    __createGLProgram: function(a, b) {
        var c = this.__context.gl;
        a = this.__createGLShader(a, c.VERTEX_SHADER);
        var d = this.__createGLShader(b, c.FRAGMENT_SHADER);
        b = c.createProgram();
        for (var f = 0, h = this.__paramFloat; f < h.length;) {
            var k = h[f];
            ++f;
            if (-1 < k.name.indexOf("Position") && O.startsWith(k.name,
                    "openfl_")) {
                c.bindAttribLocation(b, 0, k.name);
                break
            }
        }
        c.attachShader(b, a);
        c.attachShader(b, d);
        c.linkProgram(b);
        0 == c.getProgramParameter(b, c.LINK_STATUS) && (c = "Unable to initialize the shader program\n" + c.getProgramInfoLog(b), Ga.error(c, {
            fileName: "openfl/display/Shader.hx",
            lineNumber: 371,
            className: "openfl.display.Shader",
            methodName: "__createGLProgram"
        }));
        return b
    },
    __disable: function() {
        null != this.program && this.__disableGL()
    },
    __disableGL: function() {
        for (var a = this.__context.gl, b = 0, c = 0, d = this.__inputBitmapData; c <
            d.length;) {
            var f = d[c];
            ++c;
            f.__disableGL(this.__context, b);
            ++b;
            if (b == a.MAX_TEXTURE_IMAGE_UNITS) break
        }
        c = 0;
        for (d = this.__paramBool; c < d.length;) b = d[c], ++c, b.__disableGL(this.__context);
        c = 0;
        for (d = this.__paramFloat; c < d.length;) b = d[c], ++c, b.__disableGL(this.__context);
        c = 0;
        for (d = this.__paramInt; c < d.length;) b = d[c], ++c, b.__disableGL(this.__context);
        this.__context.__bindGLArrayBuffer(null);
        "opengl" == this.__context.__context.type && a.disable(a.TEXTURE_2D)
    },
    __enable: function() {
        this.__init();
        null != this.program &&
            this.__enableGL()
    },
    __enableGL: function() {
        for (var a = 0, b = this.__context.gl, c = 0, d = this.__inputBitmapData; c < d.length;) {
            var f = d[c];
            ++c;
            b.uniform1i(f.index, a);
            ++a
        }
        "opengl" == this.__context.__context.type && 0 < a && b.enable(b.TEXTURE_2D)
    },
    __init: function() {
        null == this.__data && (this.__data = zl._new(null));
        null == this.__glFragmentSource || null == this.__glVertexSource || null != this.program && !this.__glSourceDirty || this.__initGL()
    },
    __initGL: function() {
        if (this.__glSourceDirty || null == this.__paramBool) this.__glSourceDirty = !1, this.program = null, this.__inputBitmapData = [], this.__paramBool = [], this.__paramFloat = [], this.__paramInt = [], this.__processGLData(this.get_glVertexSource(), "attribute"), this.__processGLData(this.get_glVertexSource(), "uniform"), this.__processGLData(this.get_glFragmentSource(), "uniform");
        if (null != this.__context && null == this.program) {
            var a = this.__context.gl,
                b = 1 == this.precisionHint ? "precision mediump float;\n" : "precision lowp float;\n",
                c = b + this.get_glVertexSource();
            b += this.get_glFragmentSource();
            var d = c +
                b;
            Object.prototype.hasOwnProperty.call(this.__context.__programs.h, d) ? this.program = this.__context.__programs.h[d] : (this.program = this.__context.createProgram(1), this.program.__glProgram = this.__createGLProgram(c, b), this.__context.__programs.h[d] = this.program);
            if (null != this.program) {
                this.glProgram = this.program.__glProgram;
                c = 0;
                for (b = this.__inputBitmapData; c < b.length;) d = b[c], ++c, d.index = d.__isUniform ? a.getUniformLocation(this.glProgram, d.name) : a.getAttribLocation(this.glProgram, d.name);
                c = 0;
                for (b = this.__paramBool; c <
                    b.length;) d = b[c], ++c, d.index = d.__isUniform ? a.getUniformLocation(this.glProgram, d.name) : a.getAttribLocation(this.glProgram, d.name);
                c = 0;
                for (b = this.__paramFloat; c < b.length;) d = b[c], ++c, d.index = d.__isUniform ? a.getUniformLocation(this.glProgram, d.name) : a.getAttribLocation(this.glProgram, d.name);
                c = 0;
                for (b = this.__paramInt; c < b.length;) d = b[c], ++c, d.index = d.__isUniform ? a.getUniformLocation(this.glProgram, d.name) : a.getAttribLocation(this.glProgram, d.name)
            }
        }
    },
    __processGLData: function(a, b) {
        var c = 0,
            d;
        for (d = "uniform" ==
            b ? new ja("uniform ([A-Za-z0-9]+) ([A-Za-z0-9_]+)", "") : new ja("attribute ([A-Za-z0-9]+) ([A-Za-z0-9_]+)", ""); d.matchSub(a, c);) {
            var f = d.matched(1);
            var h = d.matched(2);
            if (!O.startsWith(h, "gl_")) {
                c = "uniform" == b;
                if (O.startsWith(f, "sampler")) {
                    f = new ij;
                    f.name = h;
                    f.__isUniform = c;
                    this.__inputBitmapData.push(f);
                    switch (h) {
                        case "bitmap":
                            this.__bitmap = f;
                            break;
                        case "openfl_Texture":
                            this.__texture = f
                    }
                    this.__data[h] = f;
                    this.__isGenerated && (this[h] = f)
                } else if (!Object.prototype.hasOwnProperty.call(this.__data, h) || null ==
                    ya.field(this.__data, h)) {
                    switch (f) {
                        case "bool":
                            var k = 0;
                            break;
                        case "bvec2":
                            k = 1;
                            break;
                        case "bvec3":
                            k = 2;
                            break;
                        case "bvec4":
                            k = 3;
                            break;
                        case "dvec2":
                        case "vec2":
                            k = 5;
                            break;
                        case "dvec3":
                        case "vec3":
                            k = 6;
                            break;
                        case "dvec4":
                        case "vec4":
                            k = 7;
                            break;
                        case "double":
                        case "float":
                            k = 4;
                            break;
                        case "mat2":
                        case "mat2x2":
                            k = 12;
                            break;
                        case "mat2x3":
                            k = 13;
                            break;
                        case "mat2x4":
                            k = 14;
                            break;
                        case "mat3x2":
                            k = 15;
                            break;
                        case "mat3":
                        case "mat3x3":
                            k = 16;
                            break;
                        case "mat3x4":
                            k = 17;
                            break;
                        case "mat4x2":
                            k = 18;
                            break;
                        case "mat4x3":
                            k = 19;
                            break;
                        case "mat4":
                        case "mat4x4":
                            k =
                                20;
                            break;
                        case "int":
                        case "uint":
                            k = 8;
                            break;
                        case "ivec2":
                        case "uvec2":
                            k = 9;
                            break;
                        case "ivec3":
                        case "uvec3":
                            k = 10;
                            break;
                        case "ivec4":
                        case "uvec4":
                            k = 11;
                            break;
                        default:
                            k = null
                    }
                    switch (k) {
                        case 1:
                        case 5:
                        case 9:
                            f = 2;
                            break;
                        case 2:
                        case 6:
                        case 10:
                            f = 3;
                            break;
                        case 3:
                        case 7:
                        case 11:
                        case 12:
                            f = 4;
                            break;
                        case 16:
                            f = 9;
                            break;
                        case 20:
                            f = 16;
                            break;
                        default:
                            f = 1
                    }
                    switch (k) {
                        case 12:
                            var n = 2;
                            break;
                        case 16:
                            n = 3;
                            break;
                        case 20:
                            n = 4;
                            break;
                        default:
                            n = 1
                    }
                    switch (k) {
                        case 0:
                        case 1:
                        case 2:
                        case 3:
                            var p = new Ig;
                            p.set_name(h);
                            p.type = k;
                            p.__arrayLength = n;
                            p.__isBool = !0;
                            p.__isUniform = c;
                            p.__length = f;
                            this.__paramBool.push(p);
                            "openfl_HasColorTransform" == h && (this.__hasColorTransform = p);
                            this.__data[h] = p;
                            this.__isGenerated && (this[h] = p);
                            break;
                        case 8:
                        case 9:
                        case 10:
                        case 11:
                            p = new Ig;
                            p.set_name(h);
                            p.type = k;
                            p.__arrayLength = n;
                            p.__isInt = !0;
                            p.__isUniform = c;
                            p.__length = f;
                            this.__paramInt.push(p);
                            this.__data[h] = p;
                            this.__isGenerated && (this[h] = p);
                            break;
                        default:
                            p = new Ig;
                            p.set_name(h);
                            p.type = k;
                            p.__arrayLength = n;
                            0 < n && (k = n * n, k = null != k ? new Float32Array(k) : null, p.__uniformMatrix =
                                k);
                            p.__isFloat = !0;
                            p.__isUniform = c;
                            p.__length = f;
                            this.__paramFloat.push(p);
                            if (O.startsWith(h, "openfl_")) switch (h) {
                                case "openfl_Alpha":
                                    this.__alpha = p;
                                    break;
                                case "openfl_ColorMultiplier":
                                    this.__colorMultiplier = p;
                                    break;
                                case "openfl_ColorOffset":
                                    this.__colorOffset = p;
                                    break;
                                case "openfl_Matrix":
                                    this.__matrix = p;
                                    break;
                                case "openfl_Position":
                                    this.__position = p;
                                    break;
                                case "openfl_TextureCoord":
                                    this.__textureCoord = p;
                                    break;
                                case "openfl_TextureSize":
                                    this.__textureSize = p
                            }
                            this.__data[h] = p;
                            this.__isGenerated && (this[h] =
                                p)
                    }
                }
                h = d.matchedPos();
                c = h.pos + h.len
            }
        }
    },
    __update: function() {
        null != this.program && this.__updateGL()
    },
    __updateFromBuffer: function(a, b) {
        null != this.program && this.__updateGLFromBuffer(a, b)
    },
    __updateGL: function() {
        for (var a = 0, b = 0, c = this.__inputBitmapData; b < c.length;) {
            var d = c[b];
            ++b;
            d.__updateGL(this.__context, a);
            ++a
        }
        b = 0;
        for (c = this.__paramBool; b < c.length;) a = c[b], ++b, a.__updateGL(this.__context);
        b = 0;
        for (c = this.__paramFloat; b < c.length;) a = c[b], ++b, a.__updateGL(this.__context);
        b = 0;
        for (c = this.__paramInt; b < c.length;) a =
            c[b], ++b, a.__updateGL(this.__context)
    },
    __updateGLFromBuffer: function(a, b) {
        for (var c = 0, d, f, h, k, n, p = 0, g = a.inputCount; p < g;) n = p++, d = a.inputRefs[n], f = a.inputs[n], h = a.inputFilter[n], k = a.inputMipFilter[n], n = a.inputWrap[n], null != f && (d.__updateGL(this.__context, c, f, h, k, n), ++c);
        p = this.__context.gl;
        0 < a.paramDataLength ? (null == a.paramDataBuffer && (a.paramDataBuffer = p.createBuffer()), this.__context.__bindGLArrayBuffer(a.paramDataBuffer), Lc.bufferData(p, p.ARRAY_BUFFER, a.paramData, p.DYNAMIC_DRAW)) : this.__context.__bindGLArrayBuffer(null);
        f = d = c = 0;
        h = a.paramBoolCount;
        k = a.paramFloatCount;
        var q = a.paramData,
            m = null,
            u = null,
            r = null;
        p = 0;
        for (g = a.paramCount; p < g;) {
            n = p++;
            var l = !1;
            if (n < h) {
                var D = a.paramRefs_Bool[c];
                for (var x = 0, w = a.overrideBoolCount; x < w;) {
                    var z = x++;
                    if (D.name == a.overrideBoolNames[z]) {
                        m = a.overrideBoolValues[z];
                        l = !0;
                        break
                    }
                }
                l ? D.__updateGL(this.__context, m) : D.__updateGLFromBuffer(this.__context, q, a.paramPositions[n], a.paramLengths[n], b);
                ++c
            } else if (n < h + k) {
                D = a.paramRefs_Float[d];
                x = 0;
                for (w = a.overrideFloatCount; x < w;)
                    if (z = x++, D.name == a.overrideFloatNames[z]) {
                        u =
                            a.overrideFloatValues[z];
                        l = !0;
                        break
                    } l ? D.__updateGL(this.__context, u) : D.__updateGLFromBuffer(this.__context, q, a.paramPositions[n], a.paramLengths[n], b);
                ++d
            } else {
                D = a.paramRefs_Int[f];
                x = 0;
                for (w = a.overrideIntCount; x < w;)
                    if (z = x++, D.name == a.overrideIntNames[z]) {
                        r = a.overrideIntValues[z];
                        l = !0;
                        break
                    } l ? D.__updateGL(this.__context, r) : D.__updateGLFromBuffer(this.__context, q, a.paramPositions[n], a.paramLengths[n], b);
                ++f
            }
        }
    },
    get_glFragmentSource: function() {
        return this.__glFragmentSource
    },
    get_glVertexSource: function() {
        return this.__glVertexSource
    },
    __class__: Dd,
    __properties__: {
        get_glVertexSource: "get_glVertexSource",
        get_glFragmentSource: "get_glFragmentSource"
    }
};
var jj = function(a) {
    null == this.__glFragmentSource && (this.__glFragmentSource = "varying float openfl_Alphav;\n\t\tvarying vec4 openfl_ColorMultiplierv;\n\t\tvarying vec4 openfl_ColorOffsetv;\n\t\tvarying vec2 openfl_TextureCoordv;\n\n\t\tuniform bool openfl_HasColorTransform;\n\t\tuniform sampler2D openfl_Texture;\n\t\tuniform vec2 openfl_TextureSize;\n\n\t\tvoid main(void) {\n\n\t\t\tvec4 color = texture2D (openfl_Texture, openfl_TextureCoordv);\n\n\t\tif (color.a == 0.0) {\n\n\t\t\tgl_FragColor = vec4 (0.0, 0.0, 0.0, 0.0);\n\n\t\t} else if (openfl_HasColorTransform) {\n\n\t\t\tcolor = vec4 (color.rgb / color.a, color.a);\n\n\t\t\tmat4 colorMultiplier = mat4 (0);\n\t\t\tcolorMultiplier[0][0] = openfl_ColorMultiplierv.x;\n\t\t\tcolorMultiplier[1][1] = openfl_ColorMultiplierv.y;\n\t\t\tcolorMultiplier[2][2] = openfl_ColorMultiplierv.z;\n\t\t\tcolorMultiplier[3][3] = 1.0; // openfl_ColorMultiplierv.w;\n\n\t\t\tcolor = clamp (openfl_ColorOffsetv + (color * colorMultiplier), 0.0, 1.0);\n\n\t\t\tif (color.a > 0.0) {\n\n\t\t\t\tgl_FragColor = vec4 (color.rgb * color.a * openfl_Alphav, color.a * openfl_Alphav);\n\n\t\t\t} else {\n\n\t\t\t\tgl_FragColor = vec4 (0.0, 0.0, 0.0, 0.0);\n\n\t\t\t}\n\n\t\t} else {\n\n\t\t\tgl_FragColor = color * openfl_Alphav;\n\n\t\t}\n\n\t\t}");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute float openfl_Alpha;\n\t\tattribute vec4 openfl_ColorMultiplier;\n\t\tattribute vec4 openfl_ColorOffset;\n\t\tattribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\n\t\tvarying float openfl_Alphav;\n\t\tvarying vec4 openfl_ColorMultiplierv;\n\t\tvarying vec4 openfl_ColorOffsetv;\n\t\tvarying vec2 openfl_TextureCoordv;\n\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform bool openfl_HasColorTransform;\n\t\tuniform vec2 openfl_TextureSize;\n\n\t\tvoid main(void) {\n\n\t\t\topenfl_Alphav = openfl_Alpha;\n\t\topenfl_TextureCoordv = openfl_TextureCoord;\n\n\t\tif (openfl_HasColorTransform) {\n\n\t\t\topenfl_ColorMultiplierv = openfl_ColorMultiplier;\n\t\t\topenfl_ColorOffsetv = openfl_ColorOffset / 255.0;\n\n\t\t}\n\n\t\tgl_Position = openfl_Matrix * openfl_Position;\n\n\t\t}");
    Dd.call(this, a);
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.display.DisplayObjectShader"] = jj;
jj.__name__ = "openfl.display.DisplayObjectShader";
jj.__super__ = Dd;
jj.prototype = v(Dd.prototype, {
    __class__: jj
});
var Fk = function(a, b) {
    oa.call(this);
    this.name = a;
    this.frame = b
};
g["openfl.display.FrameLabel"] = Fk;
Fk.__name__ = "openfl.display.FrameLabel";
Fk.__super__ = oa;
Fk.prototype = v(oa.prototype, {
    __class__: Fk
});
var Ed = function(a) {
    this.__dirty = !0;
    this.__owner = a;
    this.__commands = new sd;
    this.__positionY = this.__positionX =
        this.__strokePadding = 0;
    this.__renderTransform = new ua;
    this.__usedShaderBuffers = new ab;
    this.__worldTransform = new ua;
    this.__height = this.__width = 0;
    this.__bitmapScale = 1;
    this.__shaderBufferPool = new nb(function() {
        return new kj
    });
    this.moveTo(0, 0)
};
g["openfl.display.Graphics"] = Ed;
Ed.__name__ = "openfl.display.Graphics";
Ed.prototype = {
    beginFill: function(a, b) {
        null == b && (b = 1);
        null == a && (a = 0);
        this.__commands.beginFill(a & 16777215, b);
        0 < b && (this.__visible = !0)
    },
    clear: function() {
        for (var a = this.__usedShaderBuffers.h; null !=
            a;) {
            var b = a.item;
            a = a.next;
            this.__shaderBufferPool.release(b)
        }
        this.__usedShaderBuffers.clear();
        this.__commands.clear();
        this.__strokePadding = 0;
        null != this.__bounds && (this.set___dirty(!0), this.__transformDirty = !0, this.__bounds = null);
        this.__visible = !1;
        this.__positionY = this.__positionX = 0;
        this.moveTo(0, 0)
    },
    cubicCurveTo: function(a, b, c, d, f, h) {
        var k = this.__findExtrema(this.__positionX, a, c, f),
            n = this.__findExtrema(this.__positionY, b, d, h);
        this.__inflateBounds(k.min - this.__strokePadding, n.min - this.__strokePadding);
        this.__inflateBounds(k.max + this.__strokePadding, n.max + this.__strokePadding);
        this.__positionX = f;
        this.__positionY = h;
        this.__commands.cubicCurveTo(a, b, c, d, f, h);
        this.set___dirty(!0)
    },
    curveTo: function(a, b, c, d) {
        this.__inflateBounds(this.__positionX - this.__strokePadding, this.__positionY - this.__strokePadding);
        this.__inflateBounds(this.__positionX + this.__strokePadding, this.__positionY + this.__strokePadding);
        var f = a < c && a > this.__positionX || a > c && a < this.__positionX ? c : this.__calculateBezierQuadPoint((this.__positionX -
            a) / (this.__positionX - 2 * a + c), this.__positionX, a, c);
        var h = b < d && b > this.__positionY || b > d && b < this.__positionY ? d : this.__calculateBezierQuadPoint((this.__positionY - b) / (this.__positionY - 2 * b + d), this.__positionY, b, d);
        this.__inflateBounds(f - this.__strokePadding, h - this.__strokePadding);
        this.__inflateBounds(f + this.__strokePadding, h + this.__strokePadding);
        this.__positionX = c;
        this.__positionY = d;
        this.__inflateBounds(this.__positionX - this.__strokePadding, this.__positionY - this.__strokePadding);
        this.__inflateBounds(this.__positionX +
            this.__strokePadding, this.__positionY + this.__strokePadding);
        this.__commands.curveTo(a, b, c, d);
        this.set___dirty(!0)
    },
    drawCircle: function(a, b, c) {
        0 >= c || (this.__inflateBounds(a - c - this.__strokePadding, b - c - this.__strokePadding), this.__inflateBounds(a + c + this.__strokePadding, b + c + this.__strokePadding), this.__commands.drawCircle(a, b, c), this.set___dirty(!0))
    },
    drawRect: function(a, b, c, d) {
        if (0 != c || 0 != d) {
            var f = 0 > c ? -1 : 1,
                h = 0 > d ? -1 : 1;
            this.__inflateBounds(a - this.__strokePadding * f, b - this.__strokePadding * h);
            this.__inflateBounds(a +
                c + this.__strokePadding * f, b + d + this.__strokePadding * h);
            this.__commands.drawRect(a, b, c, d);
            this.set___dirty(!0)
        }
    },
    drawRoundRect: function(a, b, c, d, f, h) {
        if (0 != c || 0 != d) {
            var k = 0 > c ? -1 : 1,
                n = 0 > d ? -1 : 1;
            this.__inflateBounds(a - this.__strokePadding * k, b - this.__strokePadding * n);
            this.__inflateBounds(a + c + this.__strokePadding * k, b + d + this.__strokePadding * n);
            this.__commands.drawRoundRect(a, b, c, d, f, h);
            this.set___dirty(!0)
        }
    },
    drawRoundRectComplex: function(a, b, c, d, f, h, k, n) {
        if (!(0 >= c || 0 >= d)) {
            this.__inflateBounds(a - this.__strokePadding,
                b - this.__strokePadding);
            this.__inflateBounds(a + c + this.__strokePadding, b + d + this.__strokePadding);
            var p = a + c,
                g = b + d;
            c = c < d ? 2 * c : 2 * d;
            f < c || (f = c);
            h < c || (h = c);
            k < c || (k = c);
            n < c || (n = c);
            c = 1 - Math.sin(Math.PI / 180 * 45);
            d = 1 - Math.tan(Math.PI / 180 * 22.5);
            var q = n * c,
                m = n * d;
            this.moveTo(p, g - n);
            this.curveTo(p, g - m, p - q, g - q);
            this.curveTo(p - m, g, p - n, g);
            q = k * c;
            m = k * d;
            this.lineTo(a + k, g);
            this.curveTo(a + m, g, a + q, g - q);
            this.curveTo(a, g - m, a, g - k);
            q = f * c;
            m = f * d;
            this.lineTo(a, b + f);
            this.curveTo(a, b + m, a + q, b + q);
            this.curveTo(a + m, b, a + f, b);
            q = h * c;
            m = h * d;
            this.lineTo(p - h, b);
            this.curveTo(p - m, b, p - q, b + q);
            this.curveTo(p, b + m, p, b + h);
            this.lineTo(p, g - n);
            this.set___dirty(!0)
        }
    },
    endFill: function() {
        this.__commands.endFill()
    },
    lineStyle: function(a, b, c, d, f, h, k, n) {
        null == n && (n = 3);
        null == f && (f = 2);
        null == d && (d = !1);
        null == c && (c = 1);
        null == b && (b = 0);
        null == h && (h = 1);
        null == k && (k = 2);
        null != a && (1 == k ? a > this.__strokePadding && (this.__strokePadding = Math.ceil(a)) : a / 2 > this.__strokePadding && (this.__strokePadding = Math.ceil(a / 2)));
        this.__commands.lineStyle(a, b, c, d, f, h, k, n);
        null != a && (this.__visible = !0)
    },
    lineTo: function(a, b) {
        isFinite(a) && isFinite(b) && (this.__inflateBounds(this.__positionX - this.__strokePadding, this.__positionY - this.__strokePadding), this.__inflateBounds(this.__positionX + this.__strokePadding, this.__positionY + this.__strokePadding), this.__positionX = a, this.__positionY = b, this.__inflateBounds(this.__positionX - this.__strokePadding, this.__positionY - this.__strokePadding), this.__inflateBounds(this.__positionX + 2 * this.__strokePadding, this.__positionY + this.__strokePadding), this.__commands.lineTo(a,
            b), this.set___dirty(!0))
    },
    moveTo: function(a, b) {
        this.__positionX = a;
        this.__positionY = b;
        this.__commands.moveTo(a, b)
    },
    readGraphicsData: function(a) {
        null == a && (a = !0);
        var b = la.toObjectVector(null);
        this.__owner.__readGraphicsData(b, a);
        return b
    },
    __calculateBezierCubicPoint: function(a, b, c, d, f) {
        var h = 1 - a;
        return b * h * h * h + 3 * c * a * h * h + 3 * d * h * a * a + f * a * a * a
    },
    __calculateBezierQuadPoint: function(a, b, c, d) {
        var f = 1 - a;
        return f * f * b + 2 * f * a * c + a * a * d
    },
    __cleanup: function() {
        null != this.__bounds && null != this.__canvas && (this.set___dirty(!0),
            this.__transformDirty = !0);
        this.__context = this.__canvas = this.__bitmap = null
    },
    __getBounds: function(a, b) {
        if (null != this.__bounds) {
            var c = na.__pool.get();
            this.__bounds.__transform(c, b);
            a.__expand(c.x, c.y, c.width, c.height);
            na.__pool.release(c)
        }
    },
    __hitTest: function(a, b, c, d) {
        if (null == this.__bounds) return !1;
        var f = d.a * d.d - d.b * d.c,
            h = 0 == f ? -d.tx : 1 / f * (d.c * (d.ty - b) + d.d * (a - d.tx));
        f = d.a * d.d - d.b * d.c;
        a = 0 == f ? -d.ty : 1 / f * (d.a * (b - d.ty) + d.b * (d.tx - a));
        return h > this.__bounds.x && a > this.__bounds.y && this.__bounds.contains(h, a) ?
            c ? z.hitTest(this, h, a) : !0 : !1
    },
    __findExtrema: function(a, b, c, d) {
        var f = [];
        if (!(b < d && b > a || b > d && b < a) || !(c < d && c > a || c > d && c < a)) {
            var h = -a + 3 * b + d - 3 * c,
                k = 2 * a - 4 * b + 2 * c,
                n = b - a,
                p = k * k - 4 * h * n;
            0 == h ? (h = -n / k, 0 < h && 1 > h && f.push(this.__calculateBezierCubicPoint(h, a, b, c, d))) : 0 <= p && (n = (-k + Math.sqrt(p)) / (2 * h), h = (-k - Math.sqrt(p)) / (2 * h), 0 < n && 1 > n && f.push(this.__calculateBezierCubicPoint(n, a, b, c, d)), 0 < h && 1 > h && f.push(this.__calculateBezierCubicPoint(h, a, b, c, d)))
        }
        b = a;
        f.push(d);
        for (d = 0; d < f.length;) c = f[d], ++d, c < b && (b = c), c > a && (a = c);
        return {
            min: b,
            max: a
        }
    },
    __inflateBounds: function(a, b) {
        null == this.__bounds ? (this.__bounds = new na(a, b, 0, 0), this.__transformDirty = !0) : (a < this.__bounds.x && (this.__bounds.width += this.__bounds.x - a, this.__bounds.x = a, this.__transformDirty = !0), b < this.__bounds.y && (this.__bounds.height += this.__bounds.y - b, this.__bounds.y = b, this.__transformDirty = !0), a > this.__bounds.x + this.__bounds.width && (this.__bounds.width = a - this.__bounds.x), b > this.__bounds.y + this.__bounds.height && (this.__bounds.height = b - this.__bounds.y))
    },
    __readGraphicsData: function(a) {
        for (var b =
                new ue(this.__commands), c = null, d, f = 0, h = this.__commands.types; f < h.length;) {
            d = h[f];
            ++f;
            switch (d._hx_index) {
                case 4:
                case 5:
                case 6:
                case 7:
                case 9:
                case 10:
                case 17:
                case 18:
                    null == c && (c = new lj);
                    break;
                default:
                    null != c && (a.push(c), c = null)
            }
            switch (d._hx_index) {
                case 0:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos +=
                                4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.BEGIN_BITMAP_FILL;
                    d = b;
                    a.push(new mj(d.buffer.o[d.oPos], d.buffer.o[d.oPos + 1], d.buffer.b[d.bPos], d.buffer.b[d.bPos + 1]));
                    break;
                case 1:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos +=
                                2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos +=
                                2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.BEGIN_FILL;
                    d = b;
                    a.push(new yh(d.buffer.i[d.iPos], d.buffer.f[d.fPos]));
                    break;
                case 2:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.BEGIN_GRADIENT_FILL;
                    d = b;
                    a.push(new nj(d.buffer.o[d.oPos], d.buffer.ii[d.iiPos], d.buffer.ff[d.ffPos], d.buffer.ii[d.iiPos + 1], d.buffer.o[d.oPos + 1], d.buffer.o[d.oPos + 2], d.buffer.o[d.oPos + 3], d.buffer.f[d.fPos]));
                    break;
                case 3:
                    break;
                case 4:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos +=
                                2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos +=
                                2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.CUBIC_CURVE_TO;
                    d = b;
                    c.cubicCurveTo(d.buffer.f[d.fPos], d.buffer.f[d.fPos + 1], d.buffer.f[d.fPos + 2], d.buffer.f[d.fPos + 3], d.buffer.f[d.fPos + 4], d.buffer.f[d.fPos + 5]);
                    break;
                case 5:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos +=
                                3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.CURVE_TO;
                    d = b;
                    c.curveTo(d.buffer.f[d.fPos], d.buffer.f[d.fPos + 1], d.buffer.f[d.fPos + 2], d.buffer.f[d.fPos + 3]);
                    break;
                case 6:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos +=
                                2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_CIRCLE;
                    d = b;
                    c.__drawCircle(d.buffer.f[d.fPos], d.buffer.f[d.fPos + 1], d.buffer.f[d.fPos + 2]);
                    break;
                case 7:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_ELLIPSE;
                    d = b;
                    c.__drawEllipse(d.buffer.f[d.fPos], d.buffer.f[d.fPos + 1], d.buffer.f[d.fPos + 2], d.buffer.f[d.fPos + 3]);
                    break;
                case 9:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos +=
                                4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_RECT;
                    d = b;
                    c.__drawRect(d.buffer.f[d.fPos], d.buffer.f[d.fPos + 1], d.buffer.f[d.fPos + 2], d.buffer.f[d.fPos + 3]);
                    break;
                case 10:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos +=
                                2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.DRAW_ROUND_RECT;
                    d = b;
                    c.__drawRoundRect(d.buffer.f[d.fPos], d.buffer.f[d.fPos + 1], d.buffer.f[d.fPos + 2], d.buffer.f[d.fPos + 3], d.buffer.f[d.fPos + 4], null != d.buffer.o[d.oPos] ? d.buffer.o[d.oPos] : d.buffer.f[d.fPos + 4]);
                    break;
                case 13:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos +=
                                1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.END_FILL;
                    a.push(new oj);
                    break;
                case 14:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos +=
                                1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.LINE_BITMAP_STYLE;
                    c = null;
                    break;
                case 15:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos +=
                                4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.LINE_GRADIENT_STYLE;
                    break;
                case 16:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos +=
                                1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.LINE_STYLE;
                    var k = b;
                    d = new pj(k.buffer.o[k.oPos], k.buffer.b[k.bPos],
                        k.buffer.o[k.oPos + 1], k.buffer.o[k.oPos + 2], k.buffer.o[k.oPos + 3], k.buffer.f[k.fPos + 1]);
                    d.fill = new yh(k.buffer.i[k.iPos], k.buffer.f[k.fPos]);
                    a.push(d);
                    break;
                case 17:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.LINE_TO;
                    d = b;
                    c.lineTo(d.buffer.f[d.fPos], d.buffer.f[d.fPos + 1]);
                    break;
                case 18:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos +=
                                1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = da.MOVE_TO;
                    d = b;
                    c.moveTo(d.buffer.f[d.fPos], d.buffer.f[d.fPos +
                        1]);
                    break;
                default:
                    switch (b.prev._hx_index) {
                        case 0:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 1:
                            b.iPos += 1;
                            b.fPos += 1;
                            break;
                        case 2:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 3:
                            b.oPos += 1;
                            break;
                        case 4:
                            b.fPos += 6;
                            break;
                        case 5:
                            b.fPos += 4;
                            break;
                        case 6:
                            b.fPos += 3;
                            break;
                        case 7:
                            b.fPos += 4;
                            break;
                        case 8:
                            b.oPos += 3;
                            break;
                        case 9:
                            b.fPos += 4;
                            break;
                        case 10:
                            b.fPos += 5;
                            b.oPos += 1;
                            break;
                        case 12:
                            b.oPos += 4;
                            break;
                        case 14:
                            b.oPos += 2;
                            b.bPos += 2;
                            break;
                        case 15:
                            b.oPos += 4;
                            b.iiPos += 2;
                            b.ffPos += 1;
                            b.fPos += 1;
                            break;
                        case 16:
                            b.oPos += 4;
                            b.iPos += 1;
                            b.fPos += 2;
                            b.bPos += 1;
                            break;
                        case 17:
                            b.fPos += 2;
                            break;
                        case 18:
                            b.fPos += 2;
                            break;
                        case 19:
                            b.oPos += 1;
                            break;
                        case 20:
                            b.oPos += 1
                    }
                    b.prev = d
            }
        }
        null != c && a.push(c)
    },
    __update: function(a, b) {
        if (!(null == this.__bounds || 0 >= this.__bounds.width || 0 >= this.__bounds.height)) {
            var c = this.__owner.__renderTransform;
            if (null != c) {
                var d = b,
                    f = b;
                null == this.__owner.__worldScale9Grid && (d = 0 == c.b ? Math.abs(c.a) : Math.sqrt(c.a * c.a + c.b * c.b), f = 0 == c.c ? Math.abs(c.d) : Math.sqrt(c.c * c.c + c.d * c.d), null != a && (d = 0 == a.b ? d * a.a : d * Math.sqrt(a.a * a.a + a.b * a.b), f =
                    0 == a.c ? f * a.d : f * Math.sqrt(a.c * a.c + a.d * a.d)));
                a = this.__bounds.width * d;
                f *= this.__bounds.height;
                if (1 > a || 1 > f)(1 <= this.__width || 1 <= this.__height) && this.set___dirty(!0), this.__height = this.__width = 0;
                else {
                    null != Ed.maxTextureWidth && a > Ed.maxTextureWidth && (a = Ed.maxTextureWidth);
                    null != Ed.maxTextureWidth && f > Ed.maxTextureHeight && (f = Ed.maxTextureHeight);
                    if (null != this.__owner.__worldScale9Grid) {
                        this.__renderTransform.a = b;
                        this.__renderTransform.d = b;
                        d = 1 / b;
                        var h = 1 / b
                    } else this.__renderTransform.a = a / this.__bounds.width,
                        this.__renderTransform.d = f / this.__bounds.height, d = 1 / this.__renderTransform.a, h = 1 / this.__renderTransform.d;
                    this.__worldTransform.a = d * c.a;
                    this.__worldTransform.b = d * c.b;
                    this.__worldTransform.c = h * c.c;
                    this.__worldTransform.d = h * c.d;
                    h = this.__bounds.x;
                    var k = this.__bounds.y;
                    d = h * c.a + k * c.c + c.tx;
                    c = h * c.b + k * c.d + c.ty;
                    1 < b ? (b = 1 / b, this.__worldTransform.tx = Math.round(d / b) * b, this.__worldTransform.ty = Math.round(c / b) * b) : (this.__worldTransform.tx = Math.round(d), this.__worldTransform.ty = Math.round(c));
                    b = this.__worldTransform;
                    h = b.a * b.d - b.b * b.c;
                    this.__renderTransform.tx = 0 == h ? -b.tx : 1 / h * (b.c * (b.ty - c) + b.d * (d - b.tx));
                    b = this.__worldTransform;
                    h = b.a * b.d - b.b * b.c;
                    this.__renderTransform.ty = 0 == h ? -b.ty : 1 / h * (b.a * (c - b.ty) + b.b * (b.tx - d));
                    a = Math.ceil(a + 1);
                    f = Math.ceil(f + 1);
                    a == this.__width && f == this.__height || this.set___dirty(!0);
                    this.__width = a;
                    this.__height = f
                }
            }
        }
    },
    set___dirty: function(a) {
        if (a && null != this.__owner) {
            var b = this.__owner;
            b.__renderDirty || (b.__renderDirty = !0, b.__setParentRenderDirty())
        }
        a && (this.__hardwareDirty = this.__softwareDirty = !0);
        return this.__dirty = a
    },
    __class__: Ed,
    __properties__: {
        set___dirty: "set___dirty"
    }
};
var zh = function() {};
g["openfl.display.IGraphicsFill"] = zh;
zh.__name__ = "openfl.display.IGraphicsFill";
zh.__isInterface__ = !0;
var af = function() {};
g["openfl.display.IGraphicsData"] = af;
af.__name__ = "openfl.display.IGraphicsData";
af.__isInterface__ = !0;
af.prototype = {
    __class__: af
};
var mj = function(a, b, c, d) {
    null == d && (d = !1);
    null == c && (c = !0);
    this.bitmapData = a;
    this.matrix = b;
    this.repeat = c;
    this.smooth = d;
    this.__graphicsDataType = 4;
    this.__graphicsFillType =
        2
};
g["openfl.display.GraphicsBitmapFill"] = mj;
mj.__name__ = "openfl.display.GraphicsBitmapFill";
mj.__interfaces__ = [zh, af];
mj.prototype = {
    __class__: mj
};
var oj = function() {
    this.__graphicsDataType = 5;
    this.__graphicsFillType = 3
};
g["openfl.display.GraphicsEndFill"] = oj;
oj.__name__ = "openfl.display.GraphicsEndFill";
oj.__interfaces__ = [zh, af];
oj.prototype = {
    __class__: oj
};
var nj = function(a, b, c, d, f, h, k, n) {
    null == n && (n = 0);
    null == a && (a = 0);
    null == h && (h = 0);
    null == k && (k = 1);
    this.type = a;
    this.colors = b;
    this.alphas = c;
    this.ratios =
        d;
    this.matrix = f;
    this.spreadMethod = h;
    this.interpolationMethod = k;
    this.focalPointRatio = n;
    this.__graphicsDataType = 2;
    this.__graphicsFillType = 1
};
g["openfl.display.GraphicsGradientFill"] = nj;
nj.__name__ = "openfl.display.GraphicsGradientFill";
nj.__interfaces__ = [zh, af];
nj.prototype = {
    __class__: nj
};
var ml = function() {};
g["openfl.display.IGraphicsPath"] = ml;
ml.__name__ = "openfl.display.IGraphicsPath";
ml.__isInterface__ = !0;
var lj = function(a, b, c) {
    null == c && (c = 0);
    this.commands = a;
    this.data = b;
    this.winding = c;
    this.__graphicsDataType =
        3
};
g["openfl.display.GraphicsPath"] = lj;
lj.__name__ = "openfl.display.GraphicsPath";
lj.__interfaces__ = [ml, af];
lj.prototype = {
    cubicCurveTo: function(a, b, c, d, f, h) {
        null == this.commands && (this.commands = la.toIntVector(null));
        null == this.data && (this.data = la.toFloatVector(null));
        this.commands.push(6);
        this.data.push(a);
        this.data.push(b);
        this.data.push(c);
        this.data.push(d);
        this.data.push(f);
        this.data.push(h)
    },
    curveTo: function(a, b, c, d) {
        null == this.commands && (this.commands = la.toIntVector(null));
        null == this.data && (this.data =
            la.toFloatVector(null));
        this.commands.push(3);
        this.data.push(a);
        this.data.push(b);
        this.data.push(c);
        this.data.push(d)
    },
    lineTo: function(a, b) {
        null == this.commands && (this.commands = la.toIntVector(null));
        null == this.data && (this.data = la.toFloatVector(null));
        this.commands.push(2);
        this.data.push(a);
        this.data.push(b)
    },
    moveTo: function(a, b) {
        null == this.commands && (this.commands = la.toIntVector(null));
        null == this.data && (this.data = la.toFloatVector(null));
        this.commands.push(1);
        this.data.push(a);
        this.data.push(b)
    },
    __drawCircle: function(a,
        b, c) {
        this.__drawRoundRect(a - c, b - c, 2 * c, 2 * c, 2 * c, 2 * c)
    },
    __drawEllipse: function(a, b, c, d) {
        this.__drawRoundRect(a, b, c, d, c, d)
    },
    __drawRect: function(a, b, c, d) {
        this.moveTo(a, b);
        this.lineTo(a + c, b);
        this.lineTo(a + c, b + d);
        this.lineTo(a, b + d);
        this.lineTo(a, b)
    },
    __drawRoundRect: function(a, b, c, d, f, h) {
        f *= .5;
        h *= .5;
        f > c / 2 && (f = c / 2);
        h > d / 2 && (h = d / 2);
        c = a + c;
        d = b + d;
        var k = -f + .7071067811865476 * f,
            n = -f + .41421356237309503 * f,
            p = -h + .7071067811865476 * h,
            g = -h + .41421356237309503 * h;
        this.moveTo(c, d - h);
        this.curveTo(c, d + g, c + k, d + p);
        this.curveTo(c +
            n, d, c - f, d);
        this.lineTo(a + f, d);
        this.curveTo(a - n, d, a - k, d + p);
        this.curveTo(a, d + g, a, d - h);
        this.lineTo(a, b + h);
        this.curveTo(a, b - g, a - k, b - p);
        this.curveTo(a - n, b, a + f, b);
        this.lineTo(c - f, b);
        this.curveTo(c + n, b, c + k, b - p);
        this.curveTo(c, b - g, c, b + h);
        this.lineTo(c, d - h)
    },
    __class__: lj
};
var qj = function(a) {
    null == this.__glFragmentSource && (this.__glFragmentSource = "varying float openfl_Alphav;\n\t\tvarying vec4 openfl_ColorMultiplierv;\n\t\tvarying vec4 openfl_ColorOffsetv;\n\t\tvarying vec2 openfl_TextureCoordv;\n\n\t\tuniform bool openfl_HasColorTransform;\n\t\tuniform vec2 openfl_TextureSize;\n\t\tuniform sampler2D bitmap;\n\n\t\tvoid main(void) {\n\n\t\t\tvec4 color = texture2D (bitmap, openfl_TextureCoordv);\n\n\t\tif (color.a == 0.0) {\n\n\t\t\tgl_FragColor = vec4 (0.0, 0.0, 0.0, 0.0);\n\n\t\t} else if (openfl_HasColorTransform) {\n\n\t\t\tcolor = vec4 (color.rgb / color.a, color.a);\n\n\t\t\tmat4 colorMultiplier = mat4 (0);\n\t\t\tcolorMultiplier[0][0] = openfl_ColorMultiplierv.x;\n\t\t\tcolorMultiplier[1][1] = openfl_ColorMultiplierv.y;\n\t\t\tcolorMultiplier[2][2] = openfl_ColorMultiplierv.z;\n\t\t\tcolorMultiplier[3][3] = 1.0; // openfl_ColorMultiplierv.w;\n\n\t\t\tcolor = clamp (openfl_ColorOffsetv + (color * colorMultiplier), 0.0, 1.0);\n\n\t\t\tif (color.a > 0.0) {\n\n\t\t\t\tgl_FragColor = vec4 (color.rgb * color.a * openfl_Alphav, color.a * openfl_Alphav);\n\n\t\t\t} else {\n\n\t\t\t\tgl_FragColor = vec4 (0.0, 0.0, 0.0, 0.0);\n\n\t\t\t}\n\n\t\t} else {\n\n\t\t\tgl_FragColor = color * openfl_Alphav;\n\n\t\t}\n\n\t\t}");
    null == this.__glVertexSource && (this.__glVertexSource = "attribute float openfl_Alpha;\n\t\tattribute vec4 openfl_ColorMultiplier;\n\t\tattribute vec4 openfl_ColorOffset;\n\t\tattribute vec4 openfl_Position;\n\t\tattribute vec2 openfl_TextureCoord;\n\n\t\tvarying float openfl_Alphav;\n\t\tvarying vec4 openfl_ColorMultiplierv;\n\t\tvarying vec4 openfl_ColorOffsetv;\n\t\tvarying vec2 openfl_TextureCoordv;\n\n\t\tuniform mat4 openfl_Matrix;\n\t\tuniform bool openfl_HasColorTransform;\n\t\tuniform vec2 openfl_TextureSize;\n\n\t\tvoid main(void) {\n\n\t\t\topenfl_Alphav = openfl_Alpha;\n\t\topenfl_TextureCoordv = openfl_TextureCoord;\n\n\t\tif (openfl_HasColorTransform) {\n\n\t\t\topenfl_ColorMultiplierv = openfl_ColorMultiplier;\n\t\t\topenfl_ColorOffsetv = openfl_ColorOffset / 255.0;\n\n\t\t}\n\n\t\tgl_Position = openfl_Matrix * openfl_Position;\n\n\t\t}");
    Dd.call(this, a);
    this.__isGenerated = !0;
    this.__initGL()
};
g["openfl.display.GraphicsShader"] = qj;
qj.__name__ = "openfl.display.GraphicsShader";
qj.__super__ = Dd;
qj.prototype = v(Dd.prototype, {
    __class__: qj
});
var yh = function(a, b) {
    null == b && (b = 1);
    null == a && (a = 0);
    this.alpha = b;
    this.color = a;
    this.__graphicsDataType = 1;
    this.__graphicsFillType = 0
};
g["openfl.display.GraphicsSolidFill"] = yh;
yh.__name__ = "openfl.display.GraphicsSolidFill";
yh.__interfaces__ = [zh, af];
yh.prototype = {
    __class__: yh
};
var nl = function() {};
g["openfl.display.IGraphicsStroke"] =
    nl;
nl.__name__ = "openfl.display.IGraphicsStroke";
nl.__isInterface__ = !0;
var pj = function(a, b, c, d, f, h, k) {
    null == h && (h = 3);
    null == f && (f = 2);
    null == d && (d = 0);
    null == c && (c = 2);
    null == b && (b = !1);
    null == a && (a = NaN);
    this.caps = d;
    this.fill = k;
    this.joints = f;
    this.miterLimit = h;
    this.pixelHinting = b;
    this.scaleMode = c;
    this.thickness = a;
    this.__graphicsDataType = 0
};
g["openfl.display.GraphicsStroke"] = pj;
pj.__name__ = "openfl.display.GraphicsStroke";
pj.__interfaces__ = [nl, af];
pj.prototype = {
    __class__: pj
};
var Gk = function() {};
g["openfl.display.ITileContainer"] =
    Gk;
Gk.__name__ = "openfl.display.ITileContainer";
Gk.__isInterface__ = !0;
var ti = function(a) {
    null == a && (a = 80);
    this.quality = a
};
g["openfl.display.JPEGEncoderOptions"] = ti;
ti.__name__ = "openfl.display.JPEGEncoderOptions";
ti.prototype = {
    __class__: ti
};
var Ml = {
        toString: function(a) {
            switch (a) {
                case 0:
                    return "bevel";
                case 1:
                    return "miter";
                case 2:
                    return "round";
                default:
                    return null
            }
        }
    },
    Hk = function() {
        kb.call(this);
        this.__drawableType = 4;
        this.contentLoaderInfo = te.create(this);
        this.uncaughtErrorEvents = this.contentLoaderInfo.uncaughtErrorEvents;
        this.__unloaded = !0
    };
g["openfl.display.Loader"] = Hk;
Hk.__name__ = "openfl.display.Loader";
Hk.__super__ = kb;
Hk.prototype = v(kb.prototype, {
    addChild: function(a) {
        throw new Vb("Error #2069: The Loader class does not implement this method.", 2069);
    },
    addChildAt: function(a, b) {
        throw new Vb("Error #2069: The Loader class does not implement this method.", 2069);
    },
    removeChild: function(a) {
        if (a == this.content) return kb.prototype.removeChild.call(this, this.content);
        throw new Vb("Error #2069: The Loader class does not implement this method.",
            2069);
    },
    removeChildAt: function(a) {
        throw new Vb("Error #2069: The Loader class does not implement this method.", 2069);
    },
    __class__: Hk
});
var te = function() {
    oa.call(this);
    this.applicationDomain = Jg.currentDomain;
    this.bytesTotal = this.bytesLoaded = 0;
    this.childAllowsParent = !0;
    this.parameters = {}
};
g["openfl.display.LoaderInfo"] = te;
te.__name__ = "openfl.display.LoaderInfo";
te.create = function(a) {
    var b = new te;
    b.uncaughtErrorEvents = new rj;
    null != a ? b.loader = a : b.url = te.__rootURL;
    return b
};
te.__super__ = oa;
te.prototype =
    v(oa.prototype, {
        __complete: function() {
            this.__completed || (this.bytesLoaded < this.bytesTotal && (this.bytesLoaded = this.bytesTotal), this.__update(this.bytesLoaded, this.bytesTotal), this.__completed = !0, this.dispatchEvent(new wa("complete")))
        },
        __update: function(a, b) {
            this.bytesLoaded = a;
            this.bytesTotal = b;
            this.dispatchEvent(new Wf("progress", !1, !1, a, b))
        },
        __class__: te
    });
var Af = function() {
    ka.call(this);
    this.__enabled = !0
};
g["openfl.display.MovieClip"] = Af;
Af.__name__ = "openfl.display.MovieClip";
Af.__super__ = ka;
Af.prototype =
    v(ka.prototype, {
        gotoAndStop: function(a, b) {
            null != this.__timeline && this.__timeline.__gotoAndStop(a, b)
        },
        __enterFrame: function(a) {
            null != this.__timeline && this.__timeline.__enterFrame(a);
            for (var b = 0, c = this.__children; b < c.length;) {
                var d = c[b];
                ++b;
                d.__enterFrame(a)
            }
        },
        __tabTest: function(a) {
            this.__enabled && ka.prototype.__tabTest.call(this, a)
        },
        __onMouseDown: function(a) {
            this.__enabled && this.__hasDown && this.gotoAndStop("_down");
            this.__mouseIsDown = !0;
            null != this.stage && this.stage.addEventListener("mouseUp", l(this,
                this.__onMouseUp), !0)
        },
        __onMouseUp: function(a) {
            this.__mouseIsDown = !1;
            null != this.stage && this.stage.removeEventListener("mouseUp", l(this, this.__onMouseUp));
            this.__buttonMode && (Sd.__eq(a.target, this) && this.__enabled && this.__hasOver ? this.gotoAndStop("_over") : this.__enabled && this.__hasUp && this.gotoAndStop("_up"))
        },
        __onRollOut: function(a) {
            this.__enabled && (this.__mouseIsDown && this.__hasOver ? this.gotoAndStop("_over") : this.__hasUp && this.gotoAndStop("_up"))
        },
        __onRollOver: function(a) {
            this.__enabled && this.__hasOver &&
                this.gotoAndStop("_over")
        },
        set_buttonMode: function(a) {
            if (this.__buttonMode != a) {
                if (a) {
                    this.__hasUp = this.__hasOver = this.__hasDown = !1;
                    for (var b = 0, c = this.get_currentLabels(); b < c.length;) {
                        var d = c[b];
                        ++b;
                        switch (d.name) {
                            case "_down":
                                this.__hasDown = !0;
                                break;
                            case "_over":
                                this.__hasOver = !0;
                                break;
                            case "_up":
                                this.__hasUp = !0
                        }
                    }
                    if (this.__hasDown || this.__hasOver || this.__hasUp) this.addEventListener("rollOver", l(this, this.__onRollOver)), this.addEventListener("rollOut", l(this, this.__onRollOut)), this.addEventListener("mouseDown",
                        l(this, this.__onMouseDown))
                } else this.removeEventListener("rollOver", l(this, this.__onRollOver)), this.removeEventListener("rollOut", l(this, this.__onRollOut)), this.removeEventListener("mouseDown", l(this, this.__onMouseDown));
                this.__buttonMode = a
            }
            return a
        },
        get_currentLabels: function() {
            return null != this.__timeline ? this.__timeline.__currentLabels.slice() : []
        },
        __class__: Af,
        __properties__: v(ka.prototype.__properties__, {
            get_currentLabels: "get_currentLabels"
        })
    });
var db = function(a, b) {
    qd.call(this);
    this.__context3D =
        a;
    this.__context = a.__context;
    this.__gl = this.gl = a.__context.webgl;
    this.__defaultRenderTarget = b;
    this.__flipped = null == this.__defaultRenderTarget;
    null == Ed.maxTextureWidth && (Ed.maxTextureWidth = Ed.maxTextureHeight = this.__gl.getParameter(this.__gl.MAX_TEXTURE_SIZE));
    this.__matrix = pb._new();
    this.__values = [];
    this.__softwareRenderer = new Ue(null);
    this.__type = "opengl";
    this.__setBlendMode(10);
    this.__context3D.__setGLBlend(!0);
    this.__clipRects = [];
    this.__maskObjects = [];
    this.__numClipRects = 0;
    this.__projection = pb._new();
    this.__projectionFlipped = pb._new();
    this.__stencilReference = 0;
    this.__tempRect = new na;
    this.__defaultDisplayShader = new jj;
    this.__defaultGraphicsShader = new qj;
    this.__defaultShader = this.__defaultDisplayShader;
    this.__initShader(this.__defaultShader);
    this.__scrollRectMasks = new nb(function() {
        return new md
    });
    this.__maskShader = new Xf
};
g["openfl.display.OpenGLRenderer"] = db;
db.__name__ = "openfl.display.OpenGLRenderer";
db.__super__ = qd;
db.prototype = v(qd.prototype, {
    applyAlpha: function(a) {
        db.__alphaValue[0] = a * this.__worldAlpha;
        null != this.__currentShaderBuffer ? this.__currentShaderBuffer.addFloatOverride("openfl_Alpha", db.__alphaValue) : null != this.__currentShader && null != this.__currentShader.__alpha && (this.__currentShader.__alpha.value = db.__alphaValue)
    },
    applyBitmapData: function(a, b, c) {
        null == c && (c = !1);
        null != this.__currentShaderBuffer ? null != a && (db.__textureSizeValue[0] = a.__textureWidth, db.__textureSizeValue[1] = a.__textureHeight, this.__currentShaderBuffer.addFloatOverride("openfl_TextureSize", db.__textureSizeValue)) : null != this.__currentShader &&
            (null != this.__currentShader.__bitmap && (this.__currentShader.__bitmap.input = a, this.__currentShader.__bitmap.filter = b && this.__allowSmoothing ? 4 : 5, this.__currentShader.__bitmap.mipFilter = 2, this.__currentShader.__bitmap.wrap = c ? 2 : 0), null != this.__currentShader.__texture && (this.__currentShader.__texture.input = a, this.__currentShader.__texture.filter = b && this.__allowSmoothing ? 4 : 5, this.__currentShader.__texture.mipFilter = 2, this.__currentShader.__texture.wrap = c ? 2 : 0), null != this.__currentShader.__textureSize && (null !=
                a ? (db.__textureSizeValue[0] = a.__textureWidth, db.__textureSizeValue[1] = a.__textureHeight, this.__currentShader.__textureSize.value = db.__textureSizeValue) : this.__currentShader.__textureSize.value = null))
    },
    applyColorTransform: function(a) {
        var b = null != a && !a.__isDefault(!0);
        this.applyHasColorTransform(b);
        b ? (a.__setArrays(db.__colorMultipliersValue, db.__colorOffsetsValue), null != this.__currentShaderBuffer ? (this.__currentShaderBuffer.addFloatOverride("openfl_ColorMultiplier", db.__colorMultipliersValue), this.__currentShaderBuffer.addFloatOverride("openfl_ColorOffset",
                db.__colorOffsetsValue)) : null != this.__currentShader && (null != this.__currentShader.__colorMultiplier && (this.__currentShader.__colorMultiplier.value = db.__colorMultipliersValue), null != this.__currentShader.__colorOffset && (this.__currentShader.__colorOffset.value = db.__colorOffsetsValue))) : null != this.__currentShaderBuffer ? (this.__currentShaderBuffer.addFloatOverride("openfl_ColorMultiplier", db.__emptyColorValue), this.__currentShaderBuffer.addFloatOverride("openfl_ColorOffset", db.__emptyColorValue)) : null !=
            this.__currentShader && (null != this.__currentShader.__colorMultiplier && (this.__currentShader.__colorMultiplier.value = db.__emptyColorValue), null != this.__currentShader.__colorOffset && (this.__currentShader.__colorOffset.value = db.__emptyColorValue))
    },
    applyHasColorTransform: function(a) {
        db.__hasColorTransformValue[0] = a;
        null != this.__currentShaderBuffer ? this.__currentShaderBuffer.addBoolOverride("openfl_HasColorTransform", db.__hasColorTransformValue) : null != this.__currentShader && null != this.__currentShader.__hasColorTransform &&
            (this.__currentShader.__hasColorTransform.value = db.__hasColorTransformValue)
    },
    applyMatrix: function(a) {
        null != this.__currentShaderBuffer ? this.__currentShaderBuffer.addFloatOverride("openfl_Matrix", a) : null != this.__currentShader && null != this.__currentShader.__matrix && (this.__currentShader.__matrix.value = a)
    },
    setShader: function(a) {
        this.__currentShaderBuffer = null;
        this.__currentShader != a && (null == a ? (this.__currentShader = null, this.__context3D.setProgram(null)) : (this.__currentShader = a, this.__initShader(a), this.__context3D.setProgram(a.program),
            this.__context3D.__flushGLProgram(), this.__currentShader.__enable(), this.__context3D.__state.shader = a))
    },
    setViewport: function() {
        this.__gl.viewport(this.__offsetX, this.__offsetY, this.__displayWidth, this.__displayHeight)
    },
    updateShader: function() {
        null != this.__currentShader && (null != this.__currentShader.__position && (this.__currentShader.__position.__useArray = !0), null != this.__currentShader.__textureCoord && (this.__currentShader.__textureCoord.__useArray = !0), this.__context3D.setProgram(this.__currentShader.program),
            this.__context3D.__flushGLProgram(), this.__context3D.__flushGLTextures(), this.__currentShader.__update())
    },
    useAlphaArray: function() {
        null != this.__currentShader && null != this.__currentShader.__alpha && (this.__currentShader.__alpha.__useArray = !0)
    },
    useColorTransformArray: function() {
        null != this.__currentShader && (null != this.__currentShader.__colorMultiplier && (this.__currentShader.__colorMultiplier.__useArray = !0), null != this.__currentShader.__colorOffset && (this.__currentShader.__colorOffset.__useArray = !0))
    },
    __clear: function() {
        null == this.__stage || this.__stage.__transparent ? this.__context3D.clear(0, 0, 0, 0, 0, 0, 1) : this.__context3D.clear(this.__stage.__colorSplit[0], this.__stage.__colorSplit[1], this.__stage.__colorSplit[2], 1, 0, 0, 1);
        this.__cleared = !0
    },
    __clearShader: function() {
        null != this.__currentShader && (null == this.__currentShaderBuffer ? null != this.__currentShader.__bitmap && (this.__currentShader.__bitmap.input = null) : this.__currentShaderBuffer.clearOverride(), null != this.__currentShader.__texture && (this.__currentShader.__texture.input =
            null), null != this.__currentShader.__textureSize && (this.__currentShader.__textureSize.value = null), null != this.__currentShader.__hasColorTransform && (this.__currentShader.__hasColorTransform.value = null), null != this.__currentShader.__position && (this.__currentShader.__position.value = null), null != this.__currentShader.__matrix && (this.__currentShader.__matrix.value = null), this.__currentShader.__clearUseArray())
    },
    __copyShader: function(a) {
        this.__currentShader = a.__currentShader;
        this.__currentShaderBuffer = a.__currentShaderBuffer;
        this.__currentDisplayShader = a.__currentDisplayShader;
        this.__currentGraphicsShader = a.__currentGraphicsShader
    },
    __getMatrix: function(a, b) {
        var c = ua.__pool.get();
        c.copyFrom(a);
        c.concat(this.__worldTransform);
        if (0 == b || 1 == b && 0 == c.b && 0 == c.c && 1.001 > c.a && .999 < c.a && 1.001 > c.d && .999 < c.d) c.tx = Math.round(c.tx), c.ty = Math.round(c.ty);
        pb.identity(this.__matrix);
        pb.set(this.__matrix, 0, c.a);
        pb.set(this.__matrix, 1, c.b);
        pb.set(this.__matrix, 4, c.c);
        pb.set(this.__matrix, 5, c.d);
        pb.set(this.__matrix, 12, c.tx);
        pb.set(this.__matrix,
            13, c.ty);
        pb.append(this.__matrix, this.__flipped ? this.__projectionFlipped : this.__projection);
        this.__values[0] = pb.get(this.__matrix, 0);
        this.__values[1] = pb.get(this.__matrix, 1);
        this.__values[2] = pb.get(this.__matrix, 2);
        this.__values[3] = pb.get(this.__matrix, 3);
        this.__values[4] = pb.get(this.__matrix, 4);
        this.__values[5] = pb.get(this.__matrix, 5);
        this.__values[6] = pb.get(this.__matrix, 6);
        this.__values[7] = pb.get(this.__matrix, 7);
        this.__values[8] = pb.get(this.__matrix, 8);
        this.__values[9] = pb.get(this.__matrix, 9);
        this.__values[10] = pb.get(this.__matrix, 10);
        this.__values[11] = pb.get(this.__matrix, 11);
        this.__values[12] = pb.get(this.__matrix, 12);
        this.__values[13] = pb.get(this.__matrix, 13);
        this.__values[14] = pb.get(this.__matrix, 14);
        this.__values[15] = pb.get(this.__matrix, 15);
        ua.__pool.release(c);
        return this.__values
    },
    __initShader: function(a) {
        return null != a ? (null == a.__context && (a.__context = this.__context3D, a.__init()), a) : this.__defaultShader
    },
    __initDisplayShader: function(a) {
        return null != a ? (null == a.__context && (a.__context =
            this.__context3D, a.__init()), a) : this.__defaultDisplayShader
    },
    __initGraphicsShader: function(a) {
        return null != a ? (null == a.__context && (a.__context = this.__context3D, a.__init()), a) : this.__defaultGraphicsShader
    },
    __initShaderBuffer: function(a) {
        return null != a ? this.__initGraphicsShader(a.shader) : this.__defaultGraphicsShader
    },
    __popMask: function() {
        if (0 != this.__stencilReference) {
            var a = this.__maskObjects.pop();
            1 < this.__stencilReference ? (this.__context3D.setStencilActions(2, 1, 0, 0, 5), this.__context3D.setStencilReferenceValue(this.__stencilReference,
                255, 255), this.__context3D.setColorMask(!1, !1, !1, !1), this.__renderDrawableMask(a), this.__stencilReference--, this.__context3D.setStencilActions(2, 1, 5, 5, 5), this.__context3D.setStencilReferenceValue(this.__stencilReference, 255, 0), this.__context3D.setColorMask(!0, !0, !0, !0)) : (this.__stencilReference = 0, this.__context3D.setStencilActions(), this.__context3D.setStencilReferenceValue(0, 0, 0))
        }
    },
    __popMaskObject: function(a, b) {
        null == b && (b = !0);
        null != a.__mask && this.__popMask();
        b && null != a.__scrollRect && (0 != a.__renderTransform.b ||
            0 != a.__renderTransform.c ? (this.__scrollRectMasks.release(this.__maskObjects[this.__maskObjects.length - 1]), this.__popMask()) : this.__popMaskRect())
    },
    __popMaskRect: function() {
        0 < this.__numClipRects && (this.__numClipRects--, 0 < this.__numClipRects ? this.__scissorRect(this.__clipRects[this.__numClipRects - 1]) : this.__scissorRect())
    },
    __pushMask: function(a) {
        0 == this.__stencilReference && (this.__context3D.clear(0, 0, 0, 0, 0, 0, 4), this.__updatedStencil = !0);
        this.__context3D.setStencilActions(2, 1, 2, 5, 5);
        this.__context3D.setStencilReferenceValue(this.__stencilReference,
            255, 255);
        this.__context3D.setColorMask(!1, !1, !1, !1);
        this.__renderDrawableMask(a);
        this.__maskObjects.push(a);
        this.__stencilReference++;
        this.__context3D.setStencilActions(2, 1, 5, 5, 5);
        this.__context3D.setStencilReferenceValue(this.__stencilReference, 255, 0);
        this.__context3D.setColorMask(!0, !0, !0, !0)
    },
    __pushMaskObject: function(a, b) {
        null == b && (b = !0);
        b && null != a.__scrollRect && (0 != a.__renderTransform.b || 0 != a.__renderTransform.c ? (b = this.__scrollRectMasks.get(), b.get_graphics().clear(), b.get_graphics().beginFill(65280),
            b.get_graphics().drawRect(a.__scrollRect.x, a.__scrollRect.y, a.__scrollRect.width, a.__scrollRect.height), b.__renderTransform.copyFrom(a.__renderTransform), this.__pushMask(b)) : this.__pushMaskRect(a.__scrollRect, a.__renderTransform));
        null != a.__mask && this.__pushMask(a.__mask)
    },
    __pushMaskRect: function(a, b) {
        this.__numClipRects == this.__clipRects.length && (this.__clipRects[this.__numClipRects] = new na);
        var c = ua.__pool.get();
        c.copyFrom(b);
        c.concat(this.__worldTransform);
        b = this.__clipRects[this.__numClipRects];
        a.__transform(b, c);
        0 < this.__numClipRects && (a = this.__clipRects[this.__numClipRects - 1], b.__contract(a.x, a.y, a.width, a.height));
        0 > b.height && (b.height = 0);
        0 > b.width && (b.width = 0);
        ua.__pool.release(c);
        this.__scissorRect(b);
        this.__numClipRects++
    },
    __render: function(a) {
        this.__context3D.setColorMask(!0, !0, !0, !0);
        this.__context3D.setCulling(3);
        this.__context3D.setDepthTest(!1, 0);
        this.__context3D.setStencilActions();
        this.__context3D.setStencilReferenceValue(0, 0, 0);
        this.__context3D.setScissorRectangle(null);
        this.__blendMode =
            null;
        this.__setBlendMode(10);
        if (null == this.__defaultRenderTarget) {
            if (this.__context3D.__backBufferWantsBestResolution ? db.__scissorRectangle.setTo(this.__offsetX / this.__pixelRatio, this.__offsetY / this.__pixelRatio, this.__displayWidth / this.__pixelRatio, this.__displayHeight / this.__pixelRatio) : db.__scissorRectangle.setTo(this.__offsetX, this.__offsetY, this.__displayWidth, this.__displayHeight), this.__context3D.setScissorRectangle(db.__scissorRectangle), this.__upscaled = 1 != this.__worldTransform.a || 1 != this.__worldTransform.d,
                this.__renderDrawable(a), 0 < this.__offsetX || 0 < this.__offsetY) 0 < this.__offsetX && (db.__scissorRectangle.setTo(0, 0, this.__offsetX, this.__height), this.__context3D.setScissorRectangle(db.__scissorRectangle), this.__context3D.__flushGL(), this.__gl.clearColor(0, 0, 0, 1), this.__gl.clear(this.__gl.COLOR_BUFFER_BIT), db.__scissorRectangle.setTo(this.__offsetX + this.__displayWidth, 0, this.__width, this.__height), this.__context3D.setScissorRectangle(db.__scissorRectangle), this.__context3D.__flushGL(), this.__gl.clearColor(0,
                0, 0, 1), this.__gl.clear(this.__gl.COLOR_BUFFER_BIT)), 0 < this.__offsetY && (db.__scissorRectangle.setTo(0, 0, this.__width, this.__offsetY), this.__context3D.setScissorRectangle(db.__scissorRectangle), this.__context3D.__flushGL(), this.__gl.clearColor(0, 0, 0, 1), this.__gl.clear(this.__gl.COLOR_BUFFER_BIT), db.__scissorRectangle.setTo(0, this.__offsetY + this.__displayHeight, this.__width, this.__height), this.__context3D.setScissorRectangle(db.__scissorRectangle), this.__context3D.__flushGL(), this.__gl.clearColor(0,
                0, 0, 1), this.__gl.clear(this.__gl.COLOR_BUFFER_BIT)), this.__context3D.setScissorRectangle(null)
        } else {
            this.__context3D.__backBufferWantsBestResolution ? db.__scissorRectangle.setTo(this.__offsetX / this.__pixelRatio, this.__offsetY / this.__pixelRatio, this.__displayWidth / this.__pixelRatio, this.__displayHeight / this.__pixelRatio) : db.__scissorRectangle.setTo(this.__offsetX, this.__offsetY, this.__displayWidth, this.__displayHeight);
            this.__context3D.setScissorRectangle(db.__scissorRectangle);
            var b = a.__mask,
                c = a.__scrollRect;
            a.__mask = null;
            a.__scrollRect = null;
            this.__renderDrawable(a);
            a.__mask = b;
            a.__scrollRect = c
        }
        this.__context3D.present()
    },
    __renderDrawable: function(a) {
        if (null != a) switch (a.__drawableType) {
            case 0:
                sj.renderDrawable(a, this);
                break;
            case 2:
                Vd.renderDrawable(a, this);
                break;
            case 3:
                Yf.renderDrawable(a, this);
                break;
            case 4:
            case 5:
                tj.renderDrawable(a, this);
                break;
            case 6:
                uj.renderDrawable(a, this);
                break;
            case 7:
                bf.renderDrawable(a, this);
                break;
            case 8:
                Wd.renderDrawable(a, this);
                break;
            case 9:
                V.renderDrawable(a, this)
        }
    },
    __renderDrawableMask: function(a) {
        if (null !=
            a) switch (a.__drawableType) {
            case 0:
                sj.renderDrawableMask(a, this);
                break;
            case 2:
                Vd.renderDrawableMask(a, this);
                break;
            case 3:
                Yf.renderDrawableMask(a, this);
                break;
            case 4:
            case 5:
                tj.renderDrawableMask(a, this);
                break;
            case 6:
                uj.renderDrawableMask(a, this);
                break;
            case 7:
                bf.renderDrawableMask(a, this);
                break;
            case 8:
                Wd.renderDrawableMask(a, this);
                break;
            case 9:
                V.renderDrawableMask(a, this)
        }
    },
    __renderFilterPass: function(a, b, c, d) {
        null == d && (d = !0);
        if (null != a && null != b && null != this.__defaultRenderTarget) {
            var f = this.__context3D.__state.renderToTexture,
                h = this.__context3D.__state.renderToTextureDepthStencil,
                k = this.__context3D.__state.renderToTextureAntiAlias,
                n = this.__context3D.__state.renderToTextureSurfaceSelector;
            this.__context3D.setRenderToTexture(this.__defaultRenderTarget.getTexture(this.__context3D), !1);
            d && this.__context3D.clear(0, 0, 0, 0, 0, 0, 1);
            b = this.__initShader(b);
            this.setShader(b);
            this.applyAlpha(1);
            this.applyBitmapData(a, c);
            this.applyColorTransform(null);
            this.applyMatrix(this.__getMatrix(a.__renderTransform, 1));
            this.updateShader();
            c = a.getVertexBuffer(this.__context3D);
            null != b.__position && this.__context3D.setVertexBufferAt(b.__position.index, c, 0, 3);
            null != b.__textureCoord && this.__context3D.setVertexBufferAt(b.__textureCoord.index, c, 3, 2);
            a = a.getIndexBuffer(this.__context3D);
            this.__context3D.drawTriangles(a);
            null != f ? this.__context3D.setRenderToTexture(f, h, k, n) : this.__context3D.setRenderToBackBuffer();
            this.__clearShader()
        }
    },
    __resize: function(a, b) {
        this.__width = a;
        this.__height = b;
        a = null == this.__defaultRenderTarget ? this.__stage.stageWidth : this.__defaultRenderTarget.width;
        b = null == this.__defaultRenderTarget ? this.__stage.stageHeight : this.__defaultRenderTarget.height;
        if (null == this.__defaultRenderTarget) {
            var c = this.__worldTransform;
            c = Math.round(0 * c.a + 0 * c.c + c.tx)
        } else c = 0;
        this.__offsetX = c;
        null == this.__defaultRenderTarget ? (c = this.__worldTransform, c = Math.round(0 * c.b + 0 * c.d + c.ty)) : c = 0;
        this.__offsetY = c;
        null == this.__defaultRenderTarget ? (c = this.__worldTransform, c = Math.round(a * c.a + 0 * c.c + c.tx - this.__offsetX)) : c = a;
        this.__displayWidth = c;
        null == this.__defaultRenderTarget ? (c = this.__worldTransform,
            c = Math.round(0 * c.b + b * c.d + c.ty - this.__offsetY)) : c = b;
        this.__displayHeight = c;
        pb.createOrtho(this.__projection, 0, this.__displayWidth + 2 * this.__offsetX, 0, this.__displayHeight + 2 * this.__offsetY, -1E3, 1E3);
        pb.createOrtho(this.__projectionFlipped, 0, this.__displayWidth + 2 * this.__offsetX, this.__displayHeight + 2 * this.__offsetY, 0, -1E3, 1E3)
    },
    __resumeClipAndMask: function(a) {
        0 < this.__stencilReference ? (this.__context3D.setStencilActions(2, 1, 5, 5, 5), this.__context3D.setStencilReferenceValue(this.__stencilReference, 255,
            0)) : (this.__context3D.setStencilActions(), this.__context3D.setStencilReferenceValue(0, 0, 0));
        0 < this.__numClipRects ? this.__scissorRect(this.__clipRects[this.__numClipRects - 1]) : this.__scissorRect()
    },
    __scissorRect: function(a) {
        if (null != a) {
            var b = Math.floor(a.x),
                c = Math.floor(a.y),
                d = 0 < a.width ? Math.ceil(a.get_right()) - b : 0,
                f = 0 < a.height ? Math.ceil(a.get_bottom()) - c : 0;
            this.__context3D.__backBufferWantsBestResolution && (f = 1.5 / this.__pixelRatio, b = a.x / this.__pixelRatio, c = a.y / this.__pixelRatio, d = 0 < a.width ? a.get_right() /
                this.__pixelRatio - b + f : 0, f = 0 < a.height ? a.get_bottom() / this.__pixelRatio - c + f : 0);
            0 > d && (d = 0);
            0 > f && (f = 0);
            db.__scissorRectangle.setTo(b, c, d, f);
            this.__context3D.setScissorRectangle(db.__scissorRectangle)
        } else this.__context3D.setScissorRectangle(null)
    },
    __setBlendMode: function(a) {
        null != this.__overrideBlendMode && (a = this.__overrideBlendMode);
        if (this.__blendMode != a) switch (this.__blendMode = a, a) {
            case 0:
                this.__context3D.setBlendFactors(2, 2);
                break;
            case 9:
                this.__context3D.setBlendFactors(1, 5);
                break;
            case 12:
                this.__context3D.setBlendFactors(2,
                    6);
                break;
            case 14:
                this.__context3D.setBlendFactors(2, 2);
                this.__context3D.__setGLBlendEquation(this.__gl.FUNC_REVERSE_SUBTRACT);
                break;
            default:
                this.__context3D.setBlendFactors(2, 5)
        }
    },
    __setRenderTarget: function(a) {
        this.__defaultRenderTarget = a;
        this.__flipped = null == a;
        null != a && this.__resize(a.width, a.height)
    },
    __setShaderBuffer: function(a) {
        this.setShader(a.shader);
        this.__currentShaderBuffer = a
    },
    __suspendClipAndMask: function() {
        0 < this.__stencilReference && (this.__context3D.setStencilActions(), this.__context3D.setStencilReferenceValue(0,
            0, 0));
        0 < this.__numClipRects && this.__scissorRect()
    },
    __updateShaderBuffer: function(a) {
        null != this.__currentShader && null != this.__currentShaderBuffer && this.__currentShader.__updateFromBuffer(this.__currentShaderBuffer, a)
    },
    __class__: db
});
var ri = function(a) {
    null == a && (a = !1);
    this.fastCompression = a
};
g["openfl.display.PNGEncoderOptions"] = ri;
ri.__name__ = "openfl.display.PNGEncoderOptions";
ri.prototype = {
    __class__: ri
};
var Lh = function(a) {
    this.onComplete = new Ac;
    this.display = a;
    null != a && (a.addEventListener("unload",
        l(this, this.display_onUnload)), Ra.get_current().addChild(a))
};
g["openfl.display.Preloader"] = Lh;
Lh.__name__ = "openfl.display.Preloader";
Lh.prototype = {
    start: function() {
        this.ready = !0;
        Ra.get_current().get_loaderInfo().__complete();
        if (null != this.display) {
            var a = new wa("complete", !0, !0);
            this.display.dispatchEvent(a);
            a.isDefaultPrevented() || this.display.dispatchEvent(new wa("unload"))
        } else this.complete || (this.complete = !0, this.onComplete.dispatch())
    },
    update: function(a, b) {
        Ra.get_current().get_loaderInfo().__update(a,
            b);
        null != this.display && this.display.dispatchEvent(new Wf("progress", !0, !0, a, b))
    },
    display_onUnload: function(a) {
        null != this.display && (this.display.removeEventListener("unload", l(this, this.display_onUnload)), this.display.parent == Ra.get_current() && Ra.get_current().removeChild(this.display), Ra.get_current().stage.set_focus(null), this.display = null);
        this.ready && !this.complete && (this.complete = !0, this.onComplete.dispatch())
    },
    __class__: Lh
};
var Xg = function() {
    ka.call(this);
    var a = this.getBackgroundColor(),
        b = 0;
    70 > .299 * (a >> 16 & 255) + .587 * (a >> 8 & 255) + .114 * (a & 255) && (b = 16777215);
    a = this.getHeight() / 2 - 3.5;
    var c = this.getWidth() - 60;
    this.outline = new md;
    this.outline.get_graphics().beginFill(b, .07);
    this.outline.get_graphics().drawRect(0, 0, c, 7);
    this.outline.set_x(30);
    this.outline.set_y(a);
    this.outline.set_alpha(0);
    this.addChild(this.outline);
    this.progress = new md;
    this.progress.get_graphics().beginFill(b, .35);
    this.progress.get_graphics().drawRect(0, 0, c - 4, 3);
    this.progress.set_x(32);
    this.progress.set_y(a + 2);
    this.progress.set_scaleX(0);
    this.progress.set_alpha(0);
    this.addChild(this.progress);
    this.startAnimation = Ra.getTimer() + 100;
    this.endAnimation = this.startAnimation + 1E3;
    this.addEventListener("addedToStage", l(this, this.this_onAddedToStage))
};
g["openfl.display.DefaultPreloader"] = Xg;
Xg.__name__ = "openfl.display.DefaultPreloader";
Xg.__super__ = ka;
Xg.prototype = v(ka.prototype, {
    getBackgroundColor: function() {
        var a = Ra.get_current().stage.window.context.attributes;
        return Object.prototype.hasOwnProperty.call(a, "background") && null != a.background ?
            a.background : 0
    },
    getHeight: function() {
        var a = Ra.get_current().stage.window.__height;
        return 0 < a ? a : Ra.get_current().stage.stageHeight
    },
    getWidth: function() {
        var a = Ra.get_current().stage.window.__width;
        return 0 < a ? a : Ra.get_current().stage.stageWidth
    },
    onInit: function() {
        this.addEventListener("enterFrame", l(this, this.this_onEnterFrame))
    },
    onLoaded: function() {
        this.removeEventListener("enterFrame", l(this, this.this_onEnterFrame));
        this.dispatchEvent(new wa("unload"))
    },
    onUpdate: function(a, b) {
        var c = 0;
        0 < b && (c = a / b, 1 <
            c && (c = 1));
        this.progress.set_scaleX(c)
    },
    this_onAddedToStage: function(a) {
        this.removeEventListener("addedToStage", l(this, this.this_onAddedToStage));
        this.onInit();
        this.onUpdate(this.get_loaderInfo().bytesLoaded, this.get_loaderInfo().bytesTotal);
        this.addEventListener("progress", l(this, this.this_onProgress));
        this.addEventListener("complete", l(this, this.this_onComplete))
    },
    this_onComplete: function(a) {
        a.preventDefault();
        this.removeEventListener("progress", l(this, this.this_onProgress));
        this.removeEventListener("complete",
            l(this, this.this_onComplete));
        this.onLoaded()
    },
    this_onEnterFrame: function(a) {
        a = (Ra.getTimer() - this.startAnimation) / (this.endAnimation - this.startAnimation);
        0 > a && (a = 0);
        1 < a && (a = 1);
        this.outline.set_alpha(this.progress.set_alpha(a))
    },
    this_onProgress: function(a) {
        this.onUpdate(a.bytesLoaded | 0, a.bytesTotal | 0)
    },
    __class__: Xg
});
var zl = {
        _new: function(a) {
            return {}
        }
    },
    ij = function() {
        this.channels = 0;
        this.filter = 5;
        this.index = this.height = 0;
        this.mipFilter = 2;
        this.wrap = this.width = 0
    };
g["openfl.display.ShaderInput"] = ij;
ij.__name__ =
    "openfl.display.ShaderInput";
ij.prototype = {
    __disableGL: function(a, b) {
        0 > b || a.setTextureAt(b, null)
    },
    __updateGL: function(a, b, c, d, f, h) {
        c = null != c ? c : this.input;
        d = null != d ? d : this.filter;
        f = null != f ? f : this.mipFilter;
        h = null != h ? h : this.wrap;
        null != c ? (a.setTextureAt(b, c.getTexture(a)), a.setSamplerStateAt(b, h, d, f)) : a.setTextureAt(b, null)
    },
    __class__: ij
};
var Ig = function() {
    this.index = 0
};
g["openfl.display.ShaderParameter"] = Ig;
Ig.__name__ = "openfl.display.ShaderParameter";
Ig.prototype = {
    __disableGL: function(a) {
        if (!(0 > this.index ||
                (a = a.gl, this.__isUniform)))
            for (var b = 0, c = this.__arrayLength; b < c;) {
                var d = b++;
                a.disableVertexAttribArray(this.index + d)
            }
    },
    __updateGL: function(a, b) {
        if (!(0 > this.index)) {
            a = a.gl;
            b = null != b ? b : this.value;
            var c = this.__isBool ? b : null,
                d = this.__isFloat ? b : null,
                f = this.__isInt ? b : null;
            if (this.__isUniform)
                if (null != b && b.length >= this.__length) switch (this.type) {
                    case 0:
                        a.uniform1i(this.index, c[0] ? 1 : 0);
                        break;
                    case 1:
                        a.uniform2i(this.index, c[0] ? 1 : 0, c[1] ? 1 : 0);
                        break;
                    case 2:
                        a.uniform3i(this.index, c[0] ? 1 : 0, c[1] ? 1 : 0, c[2] ? 1 : 0);
                        break;
                    case 3:
                        a.uniform4i(this.index, c[0] ? 1 : 0, c[1] ? 1 : 0, c[2] ? 1 : 0, c[3] ? 1 : 0);
                        break;
                    case 4:
                        a.uniform1f(this.index, d[0]);
                        break;
                    case 5:
                        a.uniform2f(this.index, d[0], d[1]);
                        break;
                    case 6:
                        a.uniform3f(this.index, d[0], d[1], d[2]);
                        break;
                    case 7:
                        a.uniform4f(this.index, d[0], d[1], d[2], d[3]);
                        break;
                    case 8:
                        a.uniform1i(this.index, f[0]);
                        break;
                    case 9:
                        a.uniform2i(this.index, f[0], f[1]);
                        break;
                    case 10:
                        a.uniform3i(this.index, f[0], f[1], f[2]);
                        break;
                    case 11:
                        a.uniform4i(this.index, f[0], f[1], f[2], f[3]);
                        break;
                    case 12:
                        this.__uniformMatrix[0] =
                            d[0];
                        this.__uniformMatrix[1] = d[1];
                        this.__uniformMatrix[2] = d[2];
                        this.__uniformMatrix[3] = d[3];
                        Lc.uniformMatrix2fv(a, this.index, !1, this.__uniformMatrix);
                        break;
                    case 16:
                        this.__uniformMatrix[0] = d[0];
                        this.__uniformMatrix[1] = d[1];
                        this.__uniformMatrix[2] = d[2];
                        this.__uniformMatrix[3] = d[3];
                        this.__uniformMatrix[4] = d[4];
                        this.__uniformMatrix[5] = d[5];
                        this.__uniformMatrix[6] = d[6];
                        this.__uniformMatrix[7] = d[7];
                        this.__uniformMatrix[8] = d[8];
                        Lc.uniformMatrix3fv(a, this.index, !1, this.__uniformMatrix);
                        break;
                    case 20:
                        this.__uniformMatrix[0] =
                            d[0], this.__uniformMatrix[1] = d[1], this.__uniformMatrix[2] = d[2], this.__uniformMatrix[3] = d[3], this.__uniformMatrix[4] = d[4], this.__uniformMatrix[5] = d[5], this.__uniformMatrix[6] = d[6], this.__uniformMatrix[7] = d[7], this.__uniformMatrix[8] = d[8], this.__uniformMatrix[9] = d[9], this.__uniformMatrix[10] = d[10], this.__uniformMatrix[11] = d[11], this.__uniformMatrix[12] = d[12], this.__uniformMatrix[13] = d[13], this.__uniformMatrix[14] = d[14], this.__uniformMatrix[15] = d[15], Lc.uniformMatrix4fv(a, this.index, !1, this.__uniformMatrix)
                } else switch (this.type) {
                        case 4:
                            a.uniform1f(this.index,
                                0);
                            break;
                        case 5:
                            a.uniform2f(this.index, 0, 0);
                            break;
                        case 6:
                            a.uniform3f(this.index, 0, 0, 0);
                            break;
                        case 7:
                            a.uniform4f(this.index, 0, 0, 0, 0);
                            break;
                        case 0:
                        case 8:
                            a.uniform1i(this.index, 0);
                            break;
                        case 1:
                        case 9:
                            a.uniform2i(this.index, 0, 0);
                            break;
                        case 2:
                        case 10:
                            a.uniform3i(this.index, 0, 0, 0);
                            break;
                        case 3:
                        case 11:
                            a.uniform4i(this.index, 0, 0, 0, 0);
                            break;
                        case 12:
                            this.__uniformMatrix[0] = 0;
                            this.__uniformMatrix[1] = 0;
                            this.__uniformMatrix[2] = 0;
                            this.__uniformMatrix[3] = 0;
                            Lc.uniformMatrix2fv(a, this.index, !1, this.__uniformMatrix);
                            break;
                        case 16:
                            this.__uniformMatrix[0] = 0;
                            this.__uniformMatrix[1] = 0;
                            this.__uniformMatrix[2] = 0;
                            this.__uniformMatrix[3] = 0;
                            this.__uniformMatrix[4] = 0;
                            this.__uniformMatrix[5] = 0;
                            this.__uniformMatrix[6] = 0;
                            this.__uniformMatrix[7] = 0;
                            this.__uniformMatrix[8] = 0;
                            Lc.uniformMatrix3fv(a, this.index, !1, this.__uniformMatrix);
                            break;
                        case 20:
                            this.__uniformMatrix[0] = 0, this.__uniformMatrix[1] = 0, this.__uniformMatrix[2] = 0, this.__uniformMatrix[3] = 0, this.__uniformMatrix[4] = 0, this.__uniformMatrix[5] = 0, this.__uniformMatrix[6] = 0,
                                this.__uniformMatrix[7] = 0, this.__uniformMatrix[8] = 0, this.__uniformMatrix[9] = 0, this.__uniformMatrix[10] = 0, this.__uniformMatrix[11] = 0, this.__uniformMatrix[12] = 0, this.__uniformMatrix[13] = 0, this.__uniformMatrix[14] = 0, this.__uniformMatrix[15] = 0, Lc.uniformMatrix4fv(a, this.index, !1, this.__uniformMatrix)
                    } else if (this.__useArray || null != b && b.length != this.__length)
                        for (h = 0, k = this.__arrayLength; h < k;) n = h++, a.enableVertexAttribArray(this.index + n);
                    else {
                        for (var h = 0, k = this.__arrayLength; h < k;) {
                            var n = h++;
                            a.disableVertexAttribArray(this.index +
                                n)
                        }
                        if (null != b) switch (this.type) {
                            case 0:
                                a.vertexAttrib1f(this.index, c[0] ? 1 : 0);
                                break;
                            case 1:
                                a.vertexAttrib2f(this.index, c[0] ? 1 : 0, c[1] ? 1 : 0);
                                break;
                            case 2:
                                a.vertexAttrib3f(this.index, c[0] ? 1 : 0, c[1] ? 1 : 0, c[2] ? 1 : 0);
                                break;
                            case 3:
                                a.vertexAttrib4f(this.index, c[0] ? 1 : 0, c[1] ? 1 : 0, c[2] ? 1 : 0, c[3] ? 1 : 0);
                                break;
                            case 4:
                                a.vertexAttrib1f(this.index, d[0]);
                                break;
                            case 5:
                                a.vertexAttrib2f(this.index, d[0], d[1]);
                                break;
                            case 6:
                                a.vertexAttrib3f(this.index, d[0], d[1], d[2]);
                                break;
                            case 7:
                                a.vertexAttrib4f(this.index, d[0], d[1], d[2], d[3]);
                                break;
                            case 8:
                                a.vertexAttrib1f(this.index, f[0]);
                                break;
                            case 9:
                                a.vertexAttrib2f(this.index, f[0], f[1]);
                                break;
                            case 10:
                                a.vertexAttrib3f(this.index, f[0], f[1], f[2]);
                                break;
                            case 11:
                                a.vertexAttrib4f(this.index, f[0], f[1], f[2], f[3]);
                                break;
                            case 12:
                                a.vertexAttrib2f(this.index + 0, d[0], d[1]);
                                a.vertexAttrib2f(this.index + 1, d[2], d[3]);
                                break;
                            case 16:
                                a.vertexAttrib3f(this.index + 0, d[0], d[1], d[2]);
                                a.vertexAttrib3f(this.index + 1, d[3], d[4], d[5]);
                                a.vertexAttrib3f(this.index + 2, d[6], d[7], d[8]);
                                break;
                            case 20:
                                a.vertexAttrib4f(this.index +
                                    0, d[0], d[1], d[2], d[3]), a.vertexAttrib4f(this.index + 1, d[4], d[5], d[6], d[7]), a.vertexAttrib4f(this.index + 2, d[8], d[9], d[10], d[11]), a.vertexAttrib4f(this.index + 3, d[12], d[13], d[14], d[15])
                        } else switch (this.type) {
                            case 0:
                            case 4:
                            case 8:
                                a.vertexAttrib1f(this.index, 0);
                                break;
                            case 1:
                            case 5:
                            case 9:
                                a.vertexAttrib2f(this.index, 0, 0);
                                break;
                            case 2:
                            case 6:
                            case 10:
                                a.vertexAttrib3f(this.index, 0, 0, 0);
                                break;
                            case 3:
                            case 7:
                            case 11:
                                a.vertexAttrib4f(this.index, 0, 0, 0, 0);
                                break;
                            case 12:
                                a.vertexAttrib2f(this.index + 0, 0, 0);
                                a.vertexAttrib2f(this.index +
                                    1, 0, 0);
                                break;
                            case 16:
                                a.vertexAttrib3f(this.index + 0, 0, 0, 0);
                                a.vertexAttrib3f(this.index + 1, 0, 0, 0);
                                a.vertexAttrib3f(this.index + 2, 0, 0, 0);
                                break;
                            case 20:
                                a.vertexAttrib4f(this.index + 0, 0, 0, 0, 0), a.vertexAttrib4f(this.index + 1, 0, 0, 0, 0), a.vertexAttrib4f(this.index + 2, 0, 0, 0, 0), a.vertexAttrib4f(this.index + 3, 0, 0, 0, 0)
                        }
                    }
        }
    },
    __updateGLFromBuffer: function(a, b, c, d, f) {
        if (!(0 > this.index))
            if (a = a.gl, this.__isUniform) {
                if (d >= this.__length) switch (this.type) {
                    case 4:
                        a.uniform1f(this.index, b[c]);
                        break;
                    case 5:
                        a.uniform2f(this.index,
                            b[c], b[c + 1]);
                        break;
                    case 6:
                        a.uniform3f(this.index, b[c], b[c + 1], b[c + 2]);
                        break;
                    case 7:
                        a.uniform4f(this.index, b[c], b[c + 1], b[c + 2], b[c + 3]);
                        break;
                    case 0:
                    case 8:
                        a.uniform1i(this.index, b[c] | 0);
                        break;
                    case 1:
                    case 9:
                        a.uniform2i(this.index, b[c] | 0, b[c + 1] | 0);
                        break;
                    case 2:
                    case 10:
                        a.uniform3i(this.index, b[c] | 0, b[c + 1] | 0, b[c + 2] | 0);
                        break;
                    case 3:
                    case 11:
                        a.uniform4i(this.index, b[c] | 0, b[c + 1] | 0, b[c + 2] | 0, b[c + 3] | 0);
                        break;
                    case 12:
                        this.__uniformMatrix[0] = b[c];
                        this.__uniformMatrix[1] = b[c + 1];
                        this.__uniformMatrix[2] = b[c + 2];
                        this.__uniformMatrix[3] =
                            b[c + 3];
                        Lc.uniformMatrix2fv(a, this.index, !1, this.__uniformMatrix);
                        break;
                    case 16:
                        this.__uniformMatrix[0] = b[c];
                        this.__uniformMatrix[1] = b[c + 1];
                        this.__uniformMatrix[2] = b[c + 2];
                        this.__uniformMatrix[3] = b[c + 3];
                        this.__uniformMatrix[4] = b[c + 4];
                        this.__uniformMatrix[5] = b[c + 5];
                        this.__uniformMatrix[6] = b[c + 6];
                        this.__uniformMatrix[7] = b[c + 7];
                        this.__uniformMatrix[8] = b[c + 8];
                        Lc.uniformMatrix3fv(a, this.index, !1, this.__uniformMatrix);
                        break;
                    case 20:
                        this.__uniformMatrix[0] = b[c], this.__uniformMatrix[1] = b[c + 1], this.__uniformMatrix[2] =
                            b[c + 2], this.__uniformMatrix[3] = b[c + 3], this.__uniformMatrix[4] = b[c + 4], this.__uniformMatrix[5] = b[c + 5], this.__uniformMatrix[6] = b[c + 6], this.__uniformMatrix[7] = b[c + 7], this.__uniformMatrix[8] = b[c + 8], this.__uniformMatrix[9] = b[c + 9], this.__uniformMatrix[10] = b[c + 10], this.__uniformMatrix[11] = b[c + 11], this.__uniformMatrix[12] = b[c + 12], this.__uniformMatrix[13] = b[c + 13], this.__uniformMatrix[14] = b[c + 14], this.__uniformMatrix[15] = b[c + 15], Lc.uniformMatrix4fv(a, this.index, !1, this.__uniformMatrix)
                }
            } else if (this.__internal ||
            0 != d && d != this.__length) {
            b = a.FLOAT;
            this.__isBool ? b = a.INT : this.__isInt && (b = a.INT);
            h = 0;
            for (k = this.__arrayLength; h < k;) n = h++, a.enableVertexAttribArray(this.index + n);
            if (0 < d)
                for (h = 0, k = this.__arrayLength; h < k;) n = h++, a.vertexAttribPointer(this.index + n, this.__length, b, !1, 4 * this.__length, 4 * (c + f * this.__length + n * this.__arrayLength))
        } else {
            for (var h = 0, k = this.__arrayLength; h < k;) {
                var n = h++;
                a.disableVertexAttribArray(this.index + n)
            }
            if (0 < d) switch (this.type) {
                case 0:
                case 4:
                case 8:
                    a.vertexAttrib1f(this.index, b[c]);
                    break;
                case 1:
                case 5:
                case 9:
                    a.vertexAttrib2f(this.index, b[c], b[c + 1]);
                    break;
                case 2:
                case 6:
                case 10:
                    a.vertexAttrib3f(this.index, b[c], b[c + 1], b[c + 2]);
                    break;
                case 3:
                case 7:
                case 11:
                    a.vertexAttrib4f(this.index, b[c], b[c + 1], b[c + 2], b[c + 3]);
                    break;
                case 12:
                    a.vertexAttrib2f(this.index + 0, b[c], b[c + 1]);
                    a.vertexAttrib2f(this.index + 1, b[c + 2], b[c + 2 + 1]);
                    break;
                case 16:
                    a.vertexAttrib3f(this.index + 0, b[c], b[c + 1], b[c + 2]);
                    a.vertexAttrib3f(this.index + 1, b[c + 3], b[c + 3 + 1], b[c + 3 + 2]);
                    a.vertexAttrib3f(this.index + 2, b[c + 6], b[c + 6 + 1], b[c + 6 + 2]);
                    break;
                case 20:
                    a.vertexAttrib4f(this.index + 0, b[c], b[c + 1], b[c + 2], b[c + 3]), a.vertexAttrib4f(this.index + 1, b[c + 4], b[c + 4 + 1], b[c + 4 + 2], b[c + 4 + 3]), a.vertexAttrib4f(this.index + 2, b[c + 8], b[c + 8 + 1], b[c + 8 + 2], b[c + 8 + 3]), a.vertexAttrib4f(this.index + 3, b[c + 12], b[c + 12 + 1], b[c + 12 + 2], b[c + 12 + 3])
            } else switch (this.type) {
                case 0:
                case 4:
                case 8:
                    a.vertexAttrib1f(this.index, 0);
                    break;
                case 1:
                case 5:
                case 9:
                    a.vertexAttrib2f(this.index, 0, 0);
                    break;
                case 2:
                case 6:
                case 10:
                    a.vertexAttrib3f(this.index, 0, 0, 0);
                    break;
                case 3:
                case 7:
                case 11:
                    a.vertexAttrib4f(this.index,
                        0, 0, 0, 0);
                    break;
                case 12:
                    a.vertexAttrib2f(this.index + 0, 0, 0);
                    a.vertexAttrib2f(this.index + 1, 0, 0);
                    break;
                case 16:
                    a.vertexAttrib3f(this.index + 0, 0, 0, 0);
                    a.vertexAttrib3f(this.index + 1, 0, 0, 0);
                    a.vertexAttrib3f(this.index + 2, 0, 0, 0);
                    break;
                case 20:
                    a.vertexAttrib4f(this.index + 0, 0, 0, 0, 0), a.vertexAttrib4f(this.index + 1, 0, 0, 0, 0), a.vertexAttrib4f(this.index + 2, 0, 0, 0, 0), a.vertexAttrib4f(this.index + 3, 0, 0, 0, 0)
            }
        }
    },
    set_name: function(a) {
        this.__internal = O.startsWith(a, "openfl_");
        return this.name = a
    },
    __class__: Ig,
    __properties__: {
        set_name: "set_name"
    }
};
var Kg = function(a, b, c, d) {
    xa.call(this);
    this.__drawableType = 6;
    this.enabled = !0;
    this.trackAsMenu = !1;
    this.useHandCursor = !0;
    this.__upState = null != a ? a : new S;
    this.__overState = b;
    this.__downState = c;
    this.set_hitTestState(null != d ? d : new S);
    this.addEventListener("mouseDown", l(this, this.__this_onMouseDown));
    this.addEventListener("mouseOut", l(this, this.__this_onMouseOut));
    this.addEventListener("mouseOver", l(this, this.__this_onMouseOver));
    this.addEventListener("mouseUp", l(this, this.__this_onMouseUp));
    this.__tabEnabled = !0;
    this.set___currentState(this.__upState);
    null != Kg.__constructor && (a = Kg.__constructor, Kg.__constructor = null, a(this))
};
g["openfl.display.SimpleButton"] = Kg;
Kg.__name__ = "openfl.display.SimpleButton";
Kg.__super__ = xa;
Kg.prototype = v(xa.prototype, {
    __getBounds: function(a, b) {
        xa.prototype.__getBounds.call(this, a, b);
        var c = ua.__pool.get(),
            d = this.__currentState.__transform;
        c.a = d.a * b.a + d.b * b.c;
        c.b = d.a * b.b + d.b * b.d;
        c.c = d.c * b.a + d.d * b.c;
        c.d = d.c * b.b + d.d * b.d;
        c.tx = d.tx * b.a + d.ty * b.c + b.tx;
        c.ty = d.tx * b.b + d.ty * b.d + b.ty;
        this.__currentState.__getBounds(a,
            c);
        ua.__pool.release(c)
    },
    __getRenderBounds: function(a, b) {
        if (null != this.__scrollRect) xa.prototype.__getRenderBounds.call(this, a, b);
        else {
            xa.prototype.__getBounds.call(this, a, b);
            var c = ua.__pool.get(),
                d = this.__currentState.__transform;
            c.a = d.a * b.a + d.b * b.c;
            c.b = d.a * b.b + d.b * b.d;
            c.c = d.c * b.a + d.d * b.c;
            c.d = d.c * b.b + d.d * b.d;
            c.tx = d.tx * b.a + d.ty * b.c + b.tx;
            c.ty = d.tx * b.b + d.ty * b.d + b.ty;
            this.__currentState.__getRenderBounds(a, c);
            ua.__pool.release(c)
        }
    },
    __getCursor: function() {
        return this.useHandCursor && !this.__ignoreEvent &&
            this.enabled ? "button" : null
    },
    __hitTest: function(a, b, c, d, f, h) {
        var k = !1;
        null != this.get_hitTestState() ? this.get_hitTestState().__hitTest(a, b, c, d, f, h) && (null != d && (0 == d.length ? d[0] = h : d[d.length - 1] = h), k = !f || this.mouseEnabled) : null != this.__currentState && (!h.get_visible() || this.__isMask || f && !this.mouseEnabled || null != this.get_mask() && !this.get_mask().__hitTestMask(a, b) ? k = !1 : this.__currentState.__hitTest(a, b, c, d, f, h) && (k = f));
        if (null != d)
            for (; 1 < d.length && d[d.length - 1] == d[d.length - 2];) d.pop();
        return k
    },
    __hitTestMask: function(a,
        b) {
        var c = !1;
        this.__currentState.__hitTestMask(a, b) && (c = !0);
        return c
    },
    __setStageReference: function(a) {
        xa.prototype.__setStageReference.call(this, a);
        null != this.__currentState && this.__currentState.__setStageReference(a);
        null != this.get_hitTestState() && this.get_hitTestState() != this.__currentState && this.get_hitTestState().__setStageReference(a)
    },
    __setTransformDirty: function() {
        xa.prototype.__setTransformDirty.call(this);
        null != this.__currentState && this.__currentState.__setTransformDirty();
        null != this.get_hitTestState() &&
            this.get_hitTestState() != this.__currentState && this.get_hitTestState().__setTransformDirty()
    },
    __update: function(a, b) {
        xa.prototype.__update.call(this, a, b);
        b && (null != this.__currentState && this.__currentState.__update(a, !0), null != this.get_hitTestState() && this.get_hitTestState() != this.__currentState && this.get_hitTestState().__update(a, !0))
    },
    __updateTransforms: function(a) {
        xa.prototype.__updateTransforms.call(this, a);
        null != this.__currentState && this.__currentState.__updateTransforms();
        null != this.get_hitTestState() &&
            this.get_hitTestState() != this.__currentState && this.get_hitTestState().__updateTransforms()
    },
    get_downState: function() {
        return this.__downState
    },
    get_hitTestState: function() {
        return this.__hitTestState
    },
    set_hitTestState: function(a) {
        null != this.__hitTestState && this.__hitTestState != a && this.__hitTestState != this.get_downState() && this.__hitTestState != this.get_upState() && this.__hitTestState != this.get_overState() && (this.__hitTestState.__renderParent = null);
        null != a && (a.__renderParent = this, a.__renderDirty || (a.__renderDirty = !0, a.__setParentRenderDirty()));
        return this.__hitTestState = a
    },
    get_overState: function() {
        return this.__overState
    },
    get_upState: function() {
        return this.__upState
    },
    set___currentState: function(a) {
        null != this.__currentState && this.__currentState != this.get_hitTestState() && (this.__currentState.__renderParent = null);
        null != a && null != a.parent && a.parent.removeChild(a);
        S.__supportDOM && null == this.__previousStates && (this.__previousStates = la.toObjectVector(null));
        if (a != this.__currentState) {
            if (S.__supportDOM) {
                null != this.__currentState &&
                    (this.__currentState.__setStageReference(null), this.__previousStates.push(this.__currentState));
                var b = this.__previousStates.indexOf(a, 0);
                if (-1 < b) {
                    var c = this.__previousStates;
                    c.__tempIndex = b;
                    b = 0;
                    for (var d = []; b < d.length;) {
                        var f = d[b++];
                        c.insertAt(c.__tempIndex, f);
                        c.__tempIndex++
                    }
                    c.splice(c.__tempIndex, 1)
                }
            }
            null != a && (a.__renderParent = this, a.__renderDirty || (a.__renderDirty = !0, a.__setParentRenderDirty()));
            this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty())
        }
        return this.__currentState =
            a
    },
    __this_onMouseDown: function(a) {
        this.enabled && this.set___currentState(this.get_downState())
    },
    __this_onMouseOut: function(a) {
        this.__ignoreEvent = !1;
        this.get_upState() != this.__currentState && this.set___currentState(this.get_upState())
    },
    __this_onMouseOver: function(a) {
        a.buttonDown && (this.__ignoreEvent = !0);
        this.get_overState() != this.__currentState && null != this.get_overState() && !this.__ignoreEvent && this.enabled && this.set___currentState(this.get_overState())
    },
    __this_onMouseUp: function(a) {
        this.__ignoreEvent = !1;
        this.enabled && null != this.get_overState() ? this.set___currentState(this.get_overState()) : this.set___currentState(this.get_upState())
    },
    __class__: Kg,
    __properties__: v(xa.prototype.__properties__, {
        set___currentState: "set___currentState",
        get_upState: "get_upState",
        get_overState: "get_overState",
        set_hitTestState: "set_hitTestState",
        get_hitTestState: "get_hitTestState",
        get_downState: "get_downState"
    })
});
var Lg = function(a, b) {
    kb.call(this);
    this.__drawableType = 5;
    this.set_name(null);
    this.__color = -1;
    this.__colorSplit = [255, 255, 255];
    this.__colorString = "#FFFFFF";
    this.__contentsScaleFactor = 1;
    this.__deltaTime = this.__currentTabOrderIndex = 0;
    this.__displayState = 2;
    this.__logicalHeight = this.__logicalWidth = this.__lastClickTime = this.__mouseY = this.__mouseX = 0;
    this.__displayMatrix = new ua;
    this.__displayRect = new na;
    this.__renderDirty = !0;
    this.stage3Ds = la.toObjectVector(null);
    this.stage3Ds.push(new Zf(this));
    this.stage3Ds.push(new Zf(this));
    this.stage3Ds.push(new Zf(this));
    this.stage3Ds.push(new Zf(this));
    this.stage = this;
    this.align =
        6;
    this.allowsFullScreenInteractive = this.allowsFullScreen = !0;
    this.__quality = 1;
    this.__scaleMode = 2;
    this.showDefaultContextMenu = !0;
    this.softKeyboardRect = new na;
    this.stageFocusRect = !0;
    this.__macKeyboard = /AppleWebKit/.test(navigator.userAgent) && /Mobile\/\w+/.test(navigator.userAgent) || /Mac/.test(navigator.platform);
    this.__clearBeforeRender = !0;
    this.__forceRender = !1;
    this.__stack = [];
    this.__rollOutStack = [];
    this.__mouseOutStack = [];
    this.__touchData = new mc;
    null == Ra.get_current().__loaderInfo && (Ra.get_current().__loaderInfo =
        te.create(null), Ra.get_current().__loaderInfo.content = Ra.get_current());
    this.__uncaughtErrorEvents = Ra.get_current().__loaderInfo.uncaughtErrorEvents;
    this.application = a.application;
    this.window = a;
    this.set_color(b);
    this.__contentsScaleFactor = a.__scale;
    this.__wasFullscreen = a.__fullscreen;
    this.__resize();
    null == Ra.get_current().stage && this.stage.addChild(Ra.get_current())
};
g["openfl.display.Stage"] = Lg;
Lg.__name__ = "openfl.display.Stage";
Lg.__interfaces__ = [Za];
Lg.__super__ = kb;
Lg.prototype = v(kb.prototype, {
    localToGlobal: function(a) {
        return a.clone()
    },
    __broadcastEvent: function(a) {
        if (Object.prototype.hasOwnProperty.call(S.__broadcastEvents.h, a.type))
            for (var b = S.__broadcastEvents.h[a.type], c = 0; c < b.length;) {
                var d = b[c];
                ++c;
                if (d.stage == this || null == d.stage)
                    if (this.__uncaughtErrorEvents.__enabled) try {
                        d.__dispatch(a)
                    } catch (f) {
                        Ta.lastError = f, d = X.caught(f).unwrap(), this.__handleError(d)
                    } else d.__dispatch(a)
            }
    },
    __createRenderer: function() {
        var a = this.window.__width * this.window.__scale | 0,
            b = this.window.__height * this.window.__scale |
            0;
        switch (this.window.context.type) {
            case "canvas":
                this.__renderer = new Ue(this.window.context.canvas2D);
                break;
            case "dom":
                this.__renderer = new xh(this.window.context.dom);
                break;
            case "opengl":
            case "opengles":
            case "webgl":
                this.context3D = new ub(this), this.context3D.configureBackBuffer(this.stageWidth, this.stageHeight, 0, !0, !0, !0), this.context3D.present(), this.__renderer = new db(this.context3D)
        }
        if (null != this.__renderer) {
            var c = this.get_quality();
            this.__renderer.__allowSmoothing = 2 != c;
            this.__renderer.__pixelRatio =
                this.window.__scale;
            this.__renderer.__worldTransform = this.__displayMatrix;
            this.__renderer.__stage = this;
            this.__renderer.__resize(a, b)
        }
    },
    __dispatchEvent: function(a) {
        if (this.__uncaughtErrorEvents.__enabled) try {
            var b = kb.prototype.__dispatchEvent.call(this, a)
        } catch (c) {
            Ta.lastError = c, a = X.caught(c).unwrap(), this.__handleError(a), b = !1
        } else b = kb.prototype.__dispatchEvent.call(this, a);
        return b
    },
    __dispatchPendingMouseEvent: function() {
        this.__pendingMouseEvent && (this.__onMouse("mouseMove", this.__pendingMouseX,
            this.__pendingMouseY, 0), this.__pendingMouseEvent = !1)
    },
    __dispatchStack: function(a, b) {
        if (this.__uncaughtErrorEvents.__enabled) try {
                var c = b.length;
                if (0 == c) {
                    a.eventPhase = 2;
                    var d = a.target;
                    d.__dispatch(a)
                } else {
                    a.eventPhase = 1;
                    a.target = b[b.length - 1];
                    for (var f = 0, h = c - 1; f < h;) {
                        var k = f++;
                        b[k].__dispatch(a);
                        if (a.__isCanceled) return
                    }
                    a.eventPhase = 2;
                    d = a.target;
                    d.__dispatch(a);
                    if (!a.__isCanceled && a.bubbles)
                        for (a.eventPhase = 3, k = c - 2; 0 <= k;) {
                            b[k].__dispatch(a);
                            if (a.__isCanceled) break;
                            --k
                        }
                }
            } catch (n) {
                Ta.lastError = n, a = X.caught(n).unwrap(),
                    this.__handleError(a)
            } else if (c = b.length, 0 == c) a.eventPhase = 2, d = a.target, d.__dispatch(a);
            else {
                a.eventPhase = 1;
                a.target = b[b.length - 1];
                f = 0;
                for (h = c - 1; f < h;)
                    if (k = f++, b[k].__dispatch(a), a.__isCanceled) return;
                a.eventPhase = 2;
                d = a.target;
                d.__dispatch(a);
                if (!a.__isCanceled && a.bubbles)
                    for (a.eventPhase = 3, k = c - 2; 0 <= k;) {
                        b[k].__dispatch(a);
                        if (a.__isCanceled) break;
                        --k
                    }
            }
    },
    __dispatchTarget: function(a, b) {
        if (this.__uncaughtErrorEvents.__enabled) try {
            return a.__dispatchEvent(b)
        } catch (c) {
            return Ta.lastError = c, a = X.caught(c).unwrap(),
                this.__handleError(a), !1
        } else return a.__dispatchEvent(b)
    },
    __drag: function(a) {
        var b = this.__dragObject.parent;
        if (null != b) {
            b = b.__getWorldTransform();
            var c = b.a * b.d - b.b * b.c;
            if (0 == c) a.x = -b.tx, a.y = -b.ty;
            else {
                var d = 1 / c * (b.c * (b.ty - a.y) + b.d * (a.x - b.tx));
                a.y = 1 / c * (b.a * (a.y - b.ty) + b.b * (b.tx - a.x));
                a.x = d
            }
        }
        b = a.x + this.__dragOffsetX;
        a = a.y + this.__dragOffsetY;
        null != this.__dragBounds && (b < this.__dragBounds.x ? b = this.__dragBounds.x : b > this.__dragBounds.get_right() && (b = this.__dragBounds.get_right()), a < this.__dragBounds.y ? a =
            this.__dragBounds.y : a > this.__dragBounds.get_bottom() && (a = this.__dragBounds.get_bottom()));
        this.__dragObject.set_x(b);
        this.__dragObject.set_y(a)
    },
    __getInteractive: function(a) {
        null != a && a.push(this);
        return !0
    },
    __globalToLocal: function(a, b) {
        a != b && b.copyFrom(a);
        return b
    },
    __handleError: function(a) {
        var b = new eg("uncaughtError", !0, !0, a);
        Ra.get_current().__loaderInfo.uncaughtErrorEvents.dispatchEvent(b);
        if (!b.__preventDefault) {
            b = pf.toString(pf.exceptionStack());
            console.log(b);
            b = H.string(a);
            console.log(b);
            try {
                if (null !=
                    a && Object.prototype.hasOwnProperty.call(a, "stack") && null != a.stack && "" != a.stack) console.log(a.stack), a.stack = a.stack;
                else {
                    var c = pf.toString(pf.callStack());
                    console.log(c)
                }
            } catch (d) {
                Ta.lastError = d
            }
            throw a;
        }
    },
    __onKey: function(a, b, c) {
        this.__dispatchPendingMouseEvent();
        Ob.__altKey = Xa.get_altKey(c);
        Ob.__commandKey = Xa.get_metaKey(c);
        Ob.__controlKey = Xa.get_ctrlKey(c) && !Xa.get_metaKey(c);
        Ob.__ctrlKey = Xa.get_ctrlKey(c);
        Ob.__shiftKey = Xa.get_shiftKey(c);
        var d = [];
        null == this.__focus ? this.__getInteractive(d) : this.__focus.__getInteractive(d);
        if (0 < d.length) {
            switch (b) {
                case 1073741908:
                case 1073741909:
                case 1073741910:
                case 1073741911:
                case 1073741912:
                case 1073741913:
                case 1073741914:
                case 1073741915:
                case 1073741916:
                case 1073741917:
                case 1073741918:
                case 1073741919:
                case 1073741920:
                case 1073741921:
                case 1073741922:
                case 1073741923:
                case 1073742044:
                    var f = 3;
                    break;
                case 1073742048:
                case 1073742049:
                case 1073742050:
                case 1073742051:
                    f = 1;
                    break;
                case 1073742052:
                case 1073742053:
                case 1073742054:
                case 1073742055:
                    f = 2;
                    break;
                default:
                    f = 0
            }
            switch (b) {
                case 8:
                    b = 8;
                    break;
                case 9:
                    b =
                        9;
                    break;
                case 13:
                    b = 13;
                    break;
                case 27:
                    b = 27;
                    break;
                case 32:
                    b = 32;
                    break;
                case 33:
                    b = 49;
                    break;
                case 34:
                    b = 222;
                    break;
                case 35:
                    b = 51;
                    break;
                case 36:
                    b = 52;
                    break;
                case 37:
                    b = 53;
                    break;
                case 38:
                    b = 55;
                    break;
                case 39:
                    b = 222;
                    break;
                case 40:
                    b = 57;
                    break;
                case 41:
                    b = 48;
                    break;
                case 42:
                    b = 56;
                    break;
                case 44:
                    b = 188;
                    break;
                case 45:
                    b = 189;
                    break;
                case 46:
                    b = 190;
                    break;
                case 47:
                    b = 191;
                    break;
                case 48:
                    b = 48;
                    break;
                case 49:
                    b = 49;
                    break;
                case 50:
                    b = 50;
                    break;
                case 51:
                    b = 51;
                    break;
                case 52:
                    b = 52;
                    break;
                case 53:
                    b = 53;
                    break;
                case 54:
                    b = 54;
                    break;
                case 55:
                    b = 55;
                    break;
                case 56:
                    b = 56;
                    break;
                case 57:
                    b = 57;
                    break;
                case 58:
                    b = 186;
                    break;
                case 59:
                    b = 186;
                    break;
                case 60:
                    b = 60;
                    break;
                case 61:
                    b = 187;
                    break;
                case 62:
                    b = 190;
                    break;
                case 63:
                    b = 191;
                    break;
                case 64:
                    b = 50;
                    break;
                case 91:
                    b = 219;
                    break;
                case 92:
                    b = 220;
                    break;
                case 93:
                    b = 221;
                    break;
                case 94:
                    b = 54;
                    break;
                case 95:
                    b = 189;
                    break;
                case 96:
                    b = 192;
                    break;
                case 97:
                    b = 65;
                    break;
                case 98:
                    b = 66;
                    break;
                case 99:
                    b = 67;
                    break;
                case 100:
                    b = 68;
                    break;
                case 101:
                    b = 69;
                    break;
                case 102:
                    b = 70;
                    break;
                case 103:
                    b = 71;
                    break;
                case 104:
                    b = 72;
                    break;
                case 105:
                    b = 73;
                    break;
                case 106:
                    b = 74;
                    break;
                case 107:
                    b = 75;
                    break;
                case 108:
                    b =
                        76;
                    break;
                case 109:
                    b = 77;
                    break;
                case 110:
                    b = 78;
                    break;
                case 111:
                    b = 79;
                    break;
                case 112:
                    b = 80;
                    break;
                case 113:
                    b = 81;
                    break;
                case 114:
                    b = 82;
                    break;
                case 115:
                    b = 83;
                    break;
                case 116:
                    b = 84;
                    break;
                case 117:
                    b = 85;
                    break;
                case 118:
                    b = 86;
                    break;
                case 119:
                    b = 87;
                    break;
                case 120:
                    b = 88;
                    break;
                case 121:
                    b = 89;
                    break;
                case 122:
                    b = 90;
                    break;
                case 127:
                    b = 46;
                    break;
                case 1073741881:
                    b = 20;
                    break;
                case 1073741882:
                    b = 112;
                    break;
                case 1073741883:
                    b = 113;
                    break;
                case 1073741884:
                    b = 114;
                    break;
                case 1073741885:
                    b = 115;
                    break;
                case 1073741886:
                    b = 116;
                    break;
                case 1073741887:
                    b = 117;
                    break;
                case 1073741888:
                    b = 118;
                    break;
                case 1073741889:
                    b = 119;
                    break;
                case 1073741890:
                    b = 120;
                    break;
                case 1073741891:
                    b = 121;
                    break;
                case 1073741892:
                    b = 122;
                    break;
                case 1073741893:
                    b = 123;
                    break;
                case 1073741894:
                    b = 301;
                    break;
                case 1073741895:
                    b = 145;
                    break;
                case 1073741896:
                    b = 19;
                    break;
                case 1073741897:
                    b = 45;
                    break;
                case 1073741898:
                    b = 36;
                    break;
                case 1073741899:
                    b = 33;
                    break;
                case 1073741901:
                    b = 35;
                    break;
                case 1073741902:
                    b = 34;
                    break;
                case 1073741903:
                    b = 39;
                    break;
                case 1073741904:
                    b = 37;
                    break;
                case 1073741905:
                    b = 40;
                    break;
                case 1073741906:
                    b = 38;
                    break;
                case 1073741907:
                    b =
                        144;
                    break;
                case 1073741908:
                    b = 111;
                    break;
                case 1073741909:
                    b = 106;
                    break;
                case 1073741910:
                    b = 109;
                    break;
                case 1073741911:
                    b = 107;
                    break;
                case 1073741912:
                    b = 13;
                    break;
                case 1073741913:
                    b = 97;
                    break;
                case 1073741914:
                    b = 98;
                    break;
                case 1073741915:
                    b = 99;
                    break;
                case 1073741916:
                    b = 100;
                    break;
                case 1073741917:
                    b = 101;
                    break;
                case 1073741918:
                    b = 102;
                    break;
                case 1073741919:
                    b = 103;
                    break;
                case 1073741920:
                    b = 104;
                    break;
                case 1073741921:
                    b = 105;
                    break;
                case 1073741922:
                    b = 96;
                    break;
                case 1073741923:
                    b = 110;
                    break;
                case 1073741925:
                    b = 302;
                    break;
                case 1073741928:
                    b = 124;
                    break;
                case 1073741929:
                    b = 125;
                    break;
                case 1073741930:
                    b = 126;
                    break;
                case 1073741982:
                    b = 13;
                    break;
                case 1073742044:
                    b = 110;
                    break;
                case 1073742048:
                    b = 17;
                    break;
                case 1073742049:
                    b = 16;
                    break;
                case 1073742050:
                    b = 18;
                    break;
                case 1073742051:
                    b = 15;
                    break;
                case 1073742052:
                    b = 17;
                    break;
                case 1073742053:
                    b = 16;
                    break;
                case 1073742054:
                    b = 18;
                    break;
                case 1073742055:
                    b = 15
            }
            var h = ol.__getCharCode(b, Xa.get_shiftKey(c));
            if ("keyUp" == a && (32 == b || 13 == b) && this.__focus instanceof ka) {
                var k = va.__cast(this.__focus, ka);
                if (k.get_buttonMode() && 1 == k.focusRect) {
                    var n =
                        I.__pool.get(),
                        p = I.__pool.get();
                    p.x = this.__mouseX;
                    p.y = this.__mouseY;
                    k = Ob.__create("click", 0, 0, this.__mouseX, this.__mouseY, k.__globalToLocal(p, n), k);
                    this.__dispatchStack(k, d);
                    k.__updateAfterEventFlag && this.__renderAfterEvent();
                    I.__pool.release(p);
                    I.__pool.release(n)
                }
            }
            f = new vj(a, !0, !0, h, b, f, this.__macKeyboard ? Xa.get_ctrlKey(c) || Xa.get_metaKey(c) : Xa.get_ctrlKey(c), Xa.get_altKey(c), Xa.get_shiftKey(c), Xa.get_ctrlKey(c), Xa.get_metaKey(c));
            d.reverse();
            this.__dispatchStack(f, d);
            if (f.__preventDefault) "keyDown" ==
                a ? this.window.onKeyDown.cancel() : this.window.onKeyUp.cancel();
            else if ("keyDown" == a && 9 == b) {
                d = [];
                this.__tabTest(d);
                h = -1;
                a = null;
                b = Xa.get_shiftKey(c) ? -1 : 1;
                if (1 < d.length) {
                    Jc.sort(d, function(a, b) {
                        return a.get_tabIndex() - b.get_tabIndex()
                    });
                    if (-1 != d[d.length - 1].get_tabIndex())
                        for (p = 0; p < d.length;) {
                            if (-1 < d[p].get_tabIndex()) {
                                0 < p && d.splice(0, p);
                                break
                            }++p
                        }
                    if (null != this.get_focus()) {
                        p = this.get_focus();
                        for (n = d.indexOf(p); - 1 == n && null != p;) {
                            h = p.parent;
                            if (null != h && h.get_tabChildren()) {
                                p = h.getChildIndex(p);
                                if (-1 == p) {
                                    p =
                                        h;
                                    continue
                                }
                                for (p += b; Xa.get_shiftKey(c) ? 0 <= p : p < h.get_numChildren();) {
                                    k = h.getChildAt(p);
                                    if (k instanceof xa && (n = va.__cast(k, xa), n = d.indexOf(n), -1 != n)) {
                                        b = 0;
                                        break
                                    }
                                    p += b
                                }
                            } else Xa.get_shiftKey(c) && (n = d.indexOf(h), -1 != n && (b = 0));
                            p = h
                        }
                        h = 0 > n ? 0 : n + b
                    } else h = this.__currentTabOrderIndex
                } else 1 == d.length && (a = d[0], this.get_focus() == a && (a = null));
                n = 0 <= h && h < d.length;
                1 == d.length || 0 == d.length && null != this.get_focus() ? h = 0 : 1 < d.length && (0 > h && (h += d.length), h %= d.length, a = d[h], a == this.get_focus() && (h += b, 0 > h && (h += d.length), h %= d.length,
                    a = d[h]));
                b = null;
                null != this.get_focus() && (b = new $f("keyFocusChange", !0, !0, a, Xa.get_shiftKey(c), 0), d = [], this.get_focus().__getInteractive(d), d.reverse(), this.__dispatchStack(b, d), b.isDefaultPrevented() && this.window.onKeyDown.cancel());
                null != b && b.isDefaultPrevented() || (this.__currentTabOrderIndex = h, null != a && this.set_focus(a), n && this.window.onKeyDown.cancel())
            } else if ("keyDown" == a && null != this.get_focus() && !(this.get_focus() instanceof sc) && (this.__macKeyboard ? Xa.get_ctrlKey(c) || Xa.get_metaKey(c) : Xa.get_ctrlKey(c)) &&
                !Xa.get_altKey(c) && !Xa.get_shiftKey(c)) switch (b) {
                case 65:
                    c = new wa("selectAll", !0, !0);
                    this.get_focus().dispatchEvent(c);
                    break;
                case 67:
                    c = new wa("copy", !0, !0);
                    this.get_focus().dispatchEvent(c);
                    break;
                case 86:
                    c = new wa("paste", !0, !0);
                    this.get_focus().dispatchEvent(c);
                    break;
                case 88:
                    c = new wa("cut", !0, !0), this.get_focus().dispatchEvent(c)
            }
            f.__updateAfterEventFlag && this.__renderAfterEvent()
        }
    },
    __onLimeCreateWindow: function(a) {
        if (this.window == a) {
            var b = this;
            a.onActivate.add(function() {
                b.__onLimeWindowActivate(a)
            });
            var c = this;
            a.onClose.add(function() {
                c.__onLimeWindowClose(a)
            }, !1, -9E3);
            var d = this;
            a.onDeactivate.add(function() {
                d.__onLimeWindowDeactivate(a)
            });
            var f = this;
            var h = function(b) {
                f.__onLimeWindowDropFile(a, b)
            };
            a.onDropFile.add(h);
            var k = this;
            a.onEnter.add(function() {
                k.__onLimeWindowEnter(a)
            });
            var n = this;
            a.onExpose.add(function() {
                n.__onLimeWindowExpose(a)
            });
            var p = this;
            a.onFocusIn.add(function() {
                p.__onLimeWindowFocusIn(a)
            });
            var g = this;
            a.onFocusOut.add(function() {
                g.__onLimeWindowFocusOut(a)
            });
            var q = this;
            a.onFullscreen.add(function() {
                q.__onLimeWindowFullscreen(a)
            });
            var m = this;
            h = function(b, c) {
                m.__onLimeKeyDown(a, b, c)
            };
            a.onKeyDown.add(h);
            var u = this;
            h = function(b, c) {
                u.__onLimeKeyUp(a, b, c)
            };
            a.onKeyUp.add(h);
            var r = this;
            a.onLeave.add(function() {
                r.__onLimeWindowLeave(a)
            });
            var D = this;
            a.onMinimize.add(function() {
                D.__onLimeWindowMinimize(a)
            });
            var x = this;
            h = function(b, c, d) {
                x.__onLimeMouseDown(a, b, c, d)
            };
            a.onMouseDown.add(h);
            var w = this;
            h = function(b, c) {
                w.__onLimeMouseMove(a, b, c)
            };
            a.onMouseMove.add(h);
            var z = this;
            h = function(b, c) {
                z.__onLimeMouseMoveRelative(a, b, c)
            };
            a.onMouseMoveRelative.add(h);
            var J = this;
            h = function(b, c, d) {
                J.__onLimeMouseUp(a, b, c, d)
            };
            a.onMouseUp.add(h);
            var y = this;
            h = function(b, c, d) {
                y.__onLimeMouseWheel(a, b, c, d)
            };
            a.onMouseWheel.add(h);
            var C = this;
            h = function(b, c) {
                C.__onLimeWindowMove(a, b, c)
            };
            a.onMove.add(h);
            a.onRender.add(l(this, this.__onLimeRender));
            a.onRenderContextLost.add(l(this, this.__onLimeRenderContextLost));
            a.onRenderContextRestored.add(l(this, this.__onLimeRenderContextRestored));
            var t = this;
            h = function(b, c) {
                t.__onLimeWindowResize(a, b, c)
            };
            a.onResize.add(h);
            var v = this;
            a.onRestore.add(function() {
                v.__onLimeWindowRestore(a)
            });
            var G = this;
            h = function(b, c, d) {
                G.__onLimeTextEdit(a, b, c, d)
            };
            a.onTextEdit.add(h);
            var F = this;
            h = function(b) {
                F.__onLimeTextInput(a, b)
            };
            a.onTextInput.add(h);
            this.__onLimeWindowCreate(a)
        }
    },
    __onLimeGamepadAxisMove: function(a, b, c) {
        if (this.__uncaughtErrorEvents.__enabled) try {
            qb.__onGamepadAxisMove(a, b, c)
        } catch (d) {
            Ta.lastError = d, a = X.caught(d).unwrap(), this.__handleError(a)
        } else qb.__onGamepadAxisMove(a, b, c)
    },
    __onLimeGamepadButtonDown: function(a, b) {
        if (this.__uncaughtErrorEvents.__enabled) try {
            qb.__onGamepadButtonDown(a,
                b)
        } catch (c) {
            Ta.lastError = c, a = X.caught(c).unwrap(), this.__handleError(a)
        } else qb.__onGamepadButtonDown(a, b)
    },
    __onLimeGamepadButtonUp: function(a, b) {
        if (this.__uncaughtErrorEvents.__enabled) try {
            qb.__onGamepadButtonUp(a, b)
        } catch (c) {
            Ta.lastError = c, a = X.caught(c).unwrap(), this.__handleError(a)
        } else qb.__onGamepadButtonUp(a, b)
    },
    __onLimeGamepadConnect: function(a) {
        if (this.__uncaughtErrorEvents.__enabled) try {
            qb.__onGamepadConnect(a)
        } catch (k) {
            Ta.lastError = k;
            var b = X.caught(k).unwrap();
            this.__handleError(b)
        } else qb.__onGamepadConnect(a);
        var c = this;
        b = function(b, d) {
            c.__onLimeGamepadAxisMove(a, b, d)
        };
        a.onAxisMove.add(b);
        var d = this;
        b = function(b) {
            d.__onLimeGamepadButtonDown(a, b)
        };
        a.onButtonDown.add(b);
        var f = this;
        b = function(b) {
            f.__onLimeGamepadButtonUp(a, b)
        };
        a.onButtonUp.add(b);
        var h = this;
        a.onDisconnect.add(function() {
            h.__onLimeGamepadDisconnect(a)
        })
    },
    __onLimeGamepadDisconnect: function(a) {
        if (this.__uncaughtErrorEvents.__enabled) try {
            qb.__onGamepadDisconnect(a)
        } catch (b) {
            Ta.lastError = b, a = X.caught(b).unwrap(), this.__handleError(a)
        } else qb.__onGamepadDisconnect(a)
    },
    __onLimeKeyDown: function(a, b, c) {
        null != this.window && this.window == a && this.__onKey("keyDown", b, c)
    },
    __onLimeKeyUp: function(a, b, c) {
        null != this.window && this.window == a && this.__onKey("keyUp", b, c)
    },
    __onLimeModuleExit: function(a) {
        null != this.window && (a = new wa("deactivate"), this.__broadcastEvent(a))
    },
    __onLimeMouseDown: function(a, b, c, d) {
        if (null != this.window && this.window == a) {
            this.__dispatchPendingMouseEvent();
            switch (d) {
                case 1:
                    var f = "middleMouseDown";
                    break;
                case 2:
                    f = "rightMouseDown";
                    break;
                default:
                    f = "mouseDown"
            }
            this.__onMouse(f,
                b * a.__scale | 0, c * a.__scale | 0, d);
            this.showDefaultContextMenu || 2 != d || a.onMouseDown.cancel()
        }
    },
    __onLimeMouseMove: function(a, b, c) {
        null != this.window && this.window == a && (this.__pendingMouseEvent = !0, this.__pendingMouseX = b * a.__scale | 0, this.__pendingMouseY = c * a.__scale | 0)
    },
    __onLimeMouseMoveRelative: function(a, b, c) {},
    __onLimeMouseUp: function(a, b, c, d) {
        if (null != this.window && this.window == a) {
            this.__dispatchPendingMouseEvent();
            switch (d) {
                case 1:
                    var f = "middleMouseUp";
                    break;
                case 2:
                    f = "rightMouseUp";
                    break;
                default:
                    f = "mouseUp"
            }
            this.__onMouse(f,
                b * a.__scale | 0, c * a.__scale | 0, d);
            this.showDefaultContextMenu || 2 != d || a.onMouseUp.cancel()
        }
    },
    __onLimeMouseWheel: function(a, b, c, d) {
        null != this.window && this.window == a && (this.__dispatchPendingMouseEvent(), d == rf.PIXELS ? this.__onMouseWheel(b * a.__scale | 0, c * a.__scale | 0, d) : this.__onMouseWheel(b | 0, c | 0, d))
    },
    __renderAfterEvent: function() {
        this.__render(this.window.context)
    },
    __render: function(a) {
        a = !1;
        var b = null != this.__renderer && (this.__renderDirty || this.__forceRender);
        if (this.__invalidated && b) {
            this.__invalidated = !1;
            var c = new wa("render");
            this.__broadcastEvent(c)
        }
        this.__update(!1, !0);
        if (null != this.__renderer) {
            if (null != this.context3D) {
                for (c = this.stage3Ds.iterator(); c.hasNext();) {
                    var d = c.next();
                    this.context3D.__renderStage3D(d)
                }
                this.context3D.__present && (b = !0)
            }
            b ? (null == this.context3D && this.__renderer.__clear(), this.__renderer.__render(this)) : null == this.context3D && (a = !0);
            null != this.context3D && (this.context3D.__present ? (this.__renderer.__cleared || this.__renderer.__clear(), this.context3D.__present = !1, this.context3D.__cleared = !1) : a = !0);
            this.__renderer.__cleared = !1
        }
        return a
    },
    __onLimeRender: function(a) {
        this.__rendering || (this.__rendering = !0, this.__broadcastEvent(new wa("enterFrame")), this.__broadcastEvent(new wa("frameConstructed")), this.__broadcastEvent(new wa("exitFrame")), this.__renderable = !0, this.__enterFrame(this.__deltaTime), this.__deltaTime = 0, this.__render(a) && this.window.onRender.cancel(), this.__rendering = !1)
    },
    __onLimeRenderContextLost: function() {
        this.context3D = this.__renderer = null;
        for (var a = this.stage3Ds.iterator(); a.hasNext();) a.next().__lostContext()
    },
    __onLimeRenderContextRestored: function(a) {
        this.__createRenderer();
        for (a = this.stage3Ds.iterator(); a.hasNext();) a.next().__restoreContext()
    },
    __onLimeTextEdit: function(a, b, c, d) {},
    __onLimeTextInput: function(a, b) {
        if (null != this.window && this.window == a) {
            var c = [];
            null == this.__focus ? this.__getInteractive(c) : this.__focus.__getInteractive(c);
            b = new Ce("textInput", !0, !0, b);
            0 < c.length ? (c.reverse(), this.__dispatchStack(b, c)) : this.__dispatchEvent(b);
            b.isDefaultPrevented() && a.onTextInput.cancel()
        }
    },
    __onLimeTouchCancel: function(a) {
        var b =
            this.__primaryTouch == a;
        b && (this.__primaryTouch = null);
        this.__onTouch("touchEnd", a, b)
    },
    __onLimeTouchMove: function(a) {
        this.__onTouch("touchMove", a, this.__primaryTouch == a)
    },
    __onLimeTouchEnd: function(a) {
        var b = this.__primaryTouch == a;
        b && (this.__primaryTouch = null);
        this.__onTouch("touchEnd", a, b)
    },
    __onLimeTouchStart: function(a) {
        null == this.__primaryTouch && (this.__primaryTouch = a);
        this.__onTouch("touchBegin", a, this.__primaryTouch == a)
    },
    __onLimeUpdate: function(a) {
        this.__deltaTime = a;
        this.__dispatchPendingMouseEvent()
    },
    __onLimeWindowActivate: function(a) {},
    __onLimeWindowClose: function(a) {
        this.window == a && (this.window = null);
        this.__primaryTouch = null;
        a = new wa("deactivate");
        this.__broadcastEvent(a)
    },
    __onLimeWindowCreate: function(a) {
        null != this.window && this.window == a && null != a.context && this.__createRenderer()
    },
    __onLimeWindowDeactivate: function(a) {},
    __onLimeWindowDropFile: function(a, b) {},
    __onLimeWindowEnter: function(a) {},
    __onLimeWindowExpose: function(a) {
        null != this.window && this.window == a && (this.__renderDirty = !0)
    },
    __onLimeWindowFocusIn: function(a) {
        null !=
            this.window && this.window == a && (this.__renderDirty = !0, a = new wa("activate"), this.__broadcastEvent(a), this.set_focus(this.__cacheFocus))
    },
    __onLimeWindowFocusOut: function(a) {
        null != this.window && this.window == a && (this.__primaryTouch = null, a = new wa("deactivate"), this.__broadcastEvent(a), a = this.get_focus(), this.set_focus(null), this.__cacheFocus = a, Ob.__altKey = !1, Ob.__commandKey = !1, Ob.__ctrlKey = !1, Ob.__shiftKey = !1)
    },
    __onLimeWindowFullscreen: function(a) {
        null != this.window && this.window == a && (this.__resize(), this.__wasFullscreen ||
            (this.__wasFullscreen = !0, 2 == this.__displayState && (this.__displayState = 1), this.__dispatchEvent(new df("fullScreen", !1, !1, !0, !0))))
    },
    __onLimeWindowLeave: function(a) {
        null == this.window || this.window != a || Ob.__buttonDown || (this.__dispatchPendingMouseEvent(), a = new wa("mouseLeave"), this.__dispatchEvent(a))
    },
    __onLimeWindowMinimize: function(a) {},
    __onLimeWindowMove: function(a, b, c) {},
    __onLimeWindowResize: function(a, b, c) {
        null != this.window && this.window == a && (this.__resize(), this.__wasFullscreen && !a.__fullscreen &&
            (this.__wasFullscreen = !1, this.__displayState = 2, this.__dispatchEvent(new df("fullScreen", !1, !1, !1, !0))))
    },
    __onLimeWindowRestore: function(a) {
        null != this.window && this.window == a && this.__wasFullscreen && !a.__fullscreen && (this.__wasFullscreen = !1, this.__displayState = 2, this.__dispatchEvent(new df("fullScreen", !1, !1, !1, !0)))
    },
    __onMouse: function(a, b, c, d) {
        if (!(2 < d)) {
            var f = I.__pool.get();
            f.setTo(b, c);
            b = this.__displayMatrix;
            c = b.a * b.d - b.b * b.c;
            if (0 == c) f.x = -b.tx, f.y = -b.ty;
            else {
                var h = 1 / c * (b.c * (b.ty - f.y) + b.d * (f.x - b.tx));
                f.y = 1 / c * (b.a * (f.y - b.ty) + b.b * (b.tx - f.x));
                f.x = h
            }
            this.__mouseX = f.x;
            this.__mouseY = f.y;
            b = [];
            this.__hitTest(this.__mouseX, this.__mouseY, !0, b, !0, this) ? h = b[b.length - 1] : (h = this, b = [this]);
            null == h && (h = this);
            var k = null,
                n = !1;
            switch (a) {
                case "middleMouseDown":
                    this.__mouseDownMiddle = h;
                    n = !0;
                    break;
                case "middleMouseUp":
                    this.__mouseDownMiddle == h && (k = "middleClick");
                    this.__mouseDownMiddle = null;
                    n = !0;
                    break;
                case "mouseDown":
                    null != this.get_focus() ? this.get_focus() != h && (c = new $f("mouseFocusChange", !0, !0, h, !1, 0), this.get_focus().dispatchEvent(c),
                        c.isDefaultPrevented() || (h.__allowMouseFocus() ? this.set_focus(h) : this.set_focus(null))) : h.__allowMouseFocus() ? this.set_focus(h) : this.set_focus(null);
                    this.__mouseDownLeft = h;
                    this.__lastClickTarget != h && (this.__lastClickTarget = null, this.__lastClickTime = 0);
                    n = Ob.__buttonDown = !0;
                    break;
                case "mouseUp":
                    null != this.__mouseDownLeft && (Ob.__buttonDown = !1, this.__mouseDownLeft == h ? k = "click" : (n = Ob.__create("releaseOutside", 1, 0, this.__mouseX, this.__mouseY, new I(this.__mouseX, this.__mouseY), this), this.__mouseDownLeft.dispatchEvent(n)),
                        this.__mouseDownLeft = null);
                    n = !0;
                    break;
                case "rightMouseDown":
                    this.__mouseDownRight = h;
                    n = !0;
                    break;
                case "rightMouseUp":
                    this.__mouseDownRight == h && (k = "rightClick"), this.__mouseDownRight = null, n = !0
            }
            c = I.__pool.get();
            n = Ob.__create(a, d, n ? this.window.clickCount : 0, this.__mouseX, this.__mouseY, h.__globalToLocal(f, c), h);
            this.__dispatchStack(n, b);
            n.__updateAfterEventFlag && this.__renderAfterEvent();
            null != k && (n = Ob.__create(k, d, 0, this.__mouseX, this.__mouseY, h.__globalToLocal(f, c), h), this.__dispatchStack(n, b), n.__updateAfterEventFlag &&
                this.__renderAfterEvent(), "mouseUp" == a && (h.doubleClickEnabled ? (a = Ra.getTimer(), 500 > a - this.__lastClickTime && h == this.__lastClickTarget ? (n = Ob.__create("doubleClick", d, 0, this.__mouseX, this.__mouseY, h.__globalToLocal(f, c), h), this.__dispatchStack(n, b), n.__updateAfterEventFlag && this.__renderAfterEvent(), this.__lastClickTime = 0, this.__lastClickTarget = null) : (this.__lastClickTarget = h, this.__lastClickTime = a)) : (this.__lastClickTarget = null, this.__lastClickTime = 0)));
            if ("auto" == Ik.__cursor && !Ik.__hidden) {
                n = null;
                if (null != this.__mouseDownLeft) n = this.__mouseDownLeft.__getCursor();
                else
                    for (a = 0; a < b.length;)
                        if (n = b[a], ++a, n = n.__getCursor(), null != n && null != this.window) {
                            this.window.set_cursor(Nl.toLimeCursor(n));
                            break
                        } null == n && null != this.window && this.window.set_cursor(cc.ARROW)
            }
            h != this.__mouseOverTarget && null != this.__mouseOverTarget && (n = Ob.__create("mouseOut", d, 0, this.__mouseX, this.__mouseY, this.__mouseOverTarget.__globalToLocal(f, c), this.__mouseOverTarget), this.__dispatchStack(n, this.__mouseOutStack), n.__updateAfterEventFlag &&
                this.__renderAfterEvent());
            for (a = 0; a < this.__rollOutStack.length;) k = this.__rollOutStack[a], -1 == b.indexOf(k) ? (N.remove(this.__rollOutStack, k), n = Ob.__create("rollOut", d, 0, this.__mouseX, this.__mouseY, this.__mouseOverTarget.__globalToLocal(f, c), k), n.bubbles = !1, this.__dispatchTarget(k, n), n.__updateAfterEventFlag && this.__renderAfterEvent()) : ++a;
            for (a = 0; a < b.length;) k = b[a], ++a, -1 == this.__rollOutStack.indexOf(k) && null != this.__mouseOverTarget && (k.hasEventListener("rollOver") && (n = Ob.__create("rollOver", d, 0, this.__mouseX,
                this.__mouseY, this.__mouseOverTarget.__globalToLocal(f, c), k), n.bubbles = !1, this.__dispatchTarget(k, n), n.__updateAfterEventFlag && this.__renderAfterEvent()), (k.hasEventListener("rollOut") || k.hasEventListener("rollOver")) && this.__rollOutStack.push(k));
            h != this.__mouseOverTarget && (null != h && (n = Ob.__create("mouseOver", d, 0, this.__mouseX, this.__mouseY, h.__globalToLocal(f, c), h), this.__dispatchStack(n, b), n.__updateAfterEventFlag && this.__renderAfterEvent()), this.__mouseOverTarget = h, this.__mouseOutStack = b);
            null !=
                this.__dragObject && (this.__drag(f), d = null, this.__mouseOverTarget == this.__dragObject ? (h = this.__dragObject.mouseEnabled, a = this.__dragObject.mouseChildren, this.__dragObject.mouseEnabled = !1, this.__dragObject.mouseChildren = !1, b = [], this.__hitTest(this.__mouseX, this.__mouseY, !0, b, !0, this) && (d = b[b.length - 1]), this.__dragObject.mouseEnabled = h, this.__dragObject.mouseChildren = a) : this.__mouseOverTarget != this && (d = this.__mouseOverTarget), this.__dragObject.dropTarget = d);
            I.__pool.release(f);
            I.__pool.release(c)
        }
    },
    __onMouseWheel: function(a, b, c) {
        var d = this.__mouseX,
            f = this.__mouseY;
        a = [];
        if (this.__hitTest(this.__mouseX, this.__mouseY, !0, a, !0, this)) var h = a[a.length - 1];
        else h = this, a = [this];
        null == h && (h = this);
        c = I.__pool.get();
        c.setTo(d, f);
        d = this.__displayMatrix;
        f = d.a * d.d - d.b * d.c;
        if (0 == f) c.x = -d.tx, c.y = -d.ty;
        else {
            var k = 1 / f * (d.c * (d.ty - c.y) + d.d * (c.x - d.tx));
            c.y = 1 / f * (d.a * (c.y - d.ty) + d.b * (d.tx - c.x));
            c.x = k
        }
        b |= 0;
        b = Ob.__create("mouseWheel", 0, 0, this.__mouseX, this.__mouseY, h.__globalToLocal(c, c), h, b);
        b.cancelable = !0;
        this.__dispatchStack(b,
            a);
        b.isDefaultPrevented() && this.window.onMouseWheel.cancel();
        b.__updateAfterEventFlag && this.__renderAfterEvent();
        I.__pool.release(c)
    },
    __onTouch: function(a, b, c) {
        var d = I.__pool.get();
        d.setTo(Math.round(b.x * this.window.__width * this.window.__scale), Math.round(b.y * this.window.__height * this.window.__scale));
        var f = this.__displayMatrix,
            h = f.a * f.d - f.b * f.c;
        if (0 == h) d.x = -f.tx, d.y = -f.ty;
        else {
            var k = 1 / h * (f.c * (f.ty - d.y) + f.d * (d.x - f.tx));
            d.y = 1 / h * (f.a * (d.y - f.ty) + f.b * (f.tx - d.x));
            d.x = k
        }
        f = d.x;
        h = d.y;
        k = [];
        if (this.__hitTest(f,
                h, !1, k, !0, this)) var n = k[k.length - 1];
        else n = this, k = [this];
        null == n && (n = this);
        var p = b.id;
        if (this.__touchData.h.hasOwnProperty(p)) var g = this.__touchData.h[p];
        else g = ag.__pool.get(), g.reset(), g.touch = b, this.__touchData.h[p] = g;
        var q = null,
            m = !1;
        switch (a) {
            case "touchBegin":
                g.touchDownTarget = n;
                break;
            case "touchEnd":
                g.touchDownTarget == n && (q = "touchTap"), g.touchDownTarget = null, m = !0
        }
        var u = I.__pool.get();
        a = Zd.__create(a, null, f, h, n.__globalToLocal(d, u), n);
        a.touchPointID = p;
        a.isPrimaryTouchPoint = c;
        a.pressure = b.pressure;
        this.__dispatchStack(a, k);
        a.__updateAfterEventFlag && this.__renderAfterEvent();
        null != q && (a = Zd.__create(q, null, f, h, n.__globalToLocal(d, u), n), a.touchPointID = p, a.isPrimaryTouchPoint = c, a.pressure = b.pressure, this.__dispatchStack(a, k), a.__updateAfterEventFlag && this.__renderAfterEvent());
        q = g.touchOverTarget;
        n != q && null != q && (a = Zd.__create("touchOut", null, f, h, q.__globalToLocal(d, u), q), a.touchPointID = p, a.isPrimaryTouchPoint = c, a.pressure = b.pressure, this.__dispatchTarget(q, a), a.__updateAfterEventFlag && this.__renderAfterEvent());
        for (var r = g.rollOutStack, l, D = 0; D < r.length;) l = r[D], -1 == k.indexOf(l) ? (N.remove(r, l), a = Zd.__create("touchRollOut", null, f, h, q.__globalToLocal(d, u), q), a.touchPointID = p, a.isPrimaryTouchPoint = c, a.bubbles = !1, a.pressure = b.pressure, this.__dispatchTarget(l, a), a.__updateAfterEventFlag && this.__renderAfterEvent()) : ++D;
        for (D = 0; D < k.length;) l = k[D], ++D, -1 == r.indexOf(l) && (l.hasEventListener("touchRollOver") && (a = Zd.__create("touchRollOver", null, f, h, q.__globalToLocal(d, u), l), a.touchPointID = p, a.isPrimaryTouchPoint = c, a.bubbles = !1, a.pressure = b.pressure, this.__dispatchTarget(l, a), a.__updateAfterEventFlag && this.__renderAfterEvent()), l.hasEventListener("touchRollOut") && r.push(l));
        n != q && (null != n && (a = Zd.__create("touchOver", null, f, h, n.__globalToLocal(d, u), n), a.touchPointID = p, a.isPrimaryTouchPoint = c, a.bubbles = !0, a.pressure = b.pressure, this.__dispatchTarget(n, a), a.__updateAfterEventFlag && this.__renderAfterEvent()), g.touchOverTarget = n);
        I.__pool.release(d);
        I.__pool.release(u);
        m && (this.__touchData.remove(p), g.reset(), ag.__pool.release(g))
    },
    __registerLimeModule: function(a) {
        a.onCreateWindow.add(l(this, this.__onLimeCreateWindow));
        a.onUpdate.add(l(this, this.__onLimeUpdate));
        a.onExit.add(l(this, this.__onLimeModuleExit), !1, 0);
        for (a = qc.devices.iterator(); a.hasNext();) {
            var b = a.next();
            this.__onLimeGamepadConnect(b)
        }
        qc.onConnect.add(l(this, this.__onLimeGamepadConnect));
        dc.onStart.add(l(this, this.__onLimeTouchStart));
        dc.onMove.add(l(this, this.__onLimeTouchMove));
        dc.onEnd.add(l(this, this.__onLimeTouchEnd));
        dc.onCancel.add(l(this, this.__onLimeTouchCancel))
    },
    __resize: function() {
        var a = this.stageWidth,
            b = this.stageHeight,
            c = this.window.__width * this.window.__scale | 0,
            d = this.window.__height * this.window.__scale | 0;
        this.__displayMatrix.identity();
        if (null != this.get_fullScreenSourceRect() && this.window.__fullscreen) {
            this.stageWidth = this.get_fullScreenSourceRect().width | 0;
            this.stageHeight = this.get_fullScreenSourceRect().height | 0;
            var f = c / this.stageWidth,
                h = d / this.stageHeight;
            this.__displayMatrix.translate(-this.get_fullScreenSourceRect().x, -this.get_fullScreenSourceRect().y);
            this.__displayMatrix.scale(f, h);
            this.__displayRect.setTo(this.get_fullScreenSourceRect().get_left(), this.get_fullScreenSourceRect().get_right(), this.get_fullScreenSourceRect().get_top(), this.get_fullScreenSourceRect().get_bottom())
        } else if (0 == this.__logicalWidth || 0 == this.__logicalHeight || 2 == this.get_scaleMode() || 0 == c || 0 == d) this.stageWidth = Math.round(c / this.window.__scale), this.stageHeight = Math.round(d / this.window.__scale), this.__displayMatrix.scale(this.window.__scale, this.window.__scale), this.__displayRect.setTo(0,
            0, this.stageWidth, this.stageHeight);
        else switch (this.stageWidth = this.__logicalWidth, this.stageHeight = this.__logicalHeight, this.get_scaleMode()) {
            case 0:
                f = c / this.stageWidth;
                h = d / this.stageHeight;
                this.__displayMatrix.scale(f, h);
                this.__displayRect.setTo(0, 0, this.stageWidth, this.stageHeight);
                break;
            case 1:
                f = c / this.stageWidth;
                h = d / this.stageHeight;
                f = Math.max(f, h);
                h = this.stageWidth * f;
                var k = this.stageHeight * f;
                h = this.stageWidth - Math.round((h - c) / f);
                k = this.stageHeight - Math.round((k - d) / f);
                var n = Math.round((this.stageWidth -
                        h) / 2),
                    p = Math.round((this.stageHeight - k) / 2);
                this.__displayMatrix.translate(-n, -p);
                this.__displayMatrix.scale(f, f);
                this.__displayRect.setTo(n, p, h, k);
                break;
            default:
                f = c / this.stageWidth, h = d / this.stageHeight, f = Math.min(f, h), h = this.stageWidth * f, k = this.stageHeight * f, h = this.stageWidth - Math.round((h - c) / f), k = this.stageHeight - Math.round((k - d) / f), n = Math.round((this.stageWidth - h) / 2), p = Math.round((this.stageHeight - k) / 2), this.__displayMatrix.translate(-n, -p), this.__displayMatrix.scale(f, f), this.__displayRect.setTo(n,
                    p, h, k)
        }
        null != this.context3D && this.context3D.configureBackBuffer(this.stageWidth, this.stageHeight, 0, !0, !0, !0);
        for (f = this.stage3Ds.iterator(); f.hasNext();) f.next().__resize(c, d);
        null != this.__renderer && this.__renderer.__resize(c, d);
        this.__renderDirty = !0;
        if (this.stageWidth != a || this.stageHeight != b) this.__setTransformDirty(), a = new wa("resize"), this.__dispatchEvent(a)
    },
    __setLogicalSize: function(a, b) {
        this.__logicalWidth = a;
        this.__logicalHeight = b;
        this.__resize()
    },
    __stopDrag: function(a) {
        this.__dragObject = this.__dragBounds =
            null
    },
    __unregisterLimeModule: function(a) {
        a.onCreateWindow.remove(l(this, this.__onLimeCreateWindow));
        a.onUpdate.remove(l(this, this.__onLimeUpdate));
        a.onExit.remove(l(this, this.__onLimeModuleExit));
        qc.onConnect.remove(l(this, this.__onLimeGamepadConnect));
        dc.onStart.remove(l(this, this.__onLimeTouchStart));
        dc.onMove.remove(l(this, this.__onLimeTouchMove));
        dc.onEnd.remove(l(this, this.__onLimeTouchEnd));
        dc.onCancel.remove(l(this, this.__onLimeTouchCancel))
    },
    __update: function(a, b) {
        a ? this.__transformDirty &&
            (kb.prototype.__update.call(this, !0, b), b && (this.__transformDirty = !1)) : this.__transformDirty || this.__renderDirty ? (kb.prototype.__update.call(this, !1, b), b && S.__supportDOM && (this.__wasDirty = !0)) : !this.__renderDirty && this.__wasDirty && (kb.prototype.__update.call(this, !1, b), b && (this.__wasDirty = !1))
    },
    set_color: function(a) {
        null == a ? (this.__transparent = !0, a = 0) : this.__transparent = !1;
        this.__color != a && (this.__colorSplit[0] = ((a & 16711680) >>> 16) / 255, this.__colorSplit[1] = ((a & 65280) >>> 8) / 255, this.__colorSplit[2] = (a &
            255) / 255, this.__colorString = "#" + O.hex(a & 16777215, 6), this.__renderDirty = !0, this.__color = -16777216 | a & 16777215);
        return a
    },
    get_focus: function() {
        return this.__focus
    },
    set_focus: function(a) {
        if (a != this.__focus || null == a && null != this.__cacheFocus) {
            var b = this.__focus;
            this.__cacheFocus = this.__focus = a;
            if (null != b) {
                var c = new $f("focusOut", !0, !1, a, !1, 0),
                    d = [];
                b.__getInteractive(d);
                d.reverse();
                this.__dispatchStack(c, d)
            }
            null != a && (c = new $f("focusIn", !0, !1, b, !1, 0), d = [], a.__getInteractive(d), d.reverse(), this.__dispatchStack(c,
                d))
        }
        return a
    },
    get_frameRate: function() {
        return null != this.window ? this.window.__backend.getFrameRate() : 0
    },
    get_fullScreenSourceRect: function() {
        return null == this.__fullScreenSourceRect ? null : this.__fullScreenSourceRect.clone()
    },
    set_height: function(a) {
        return this.get_height()
    },
    get_mouseX: function() {
        return this.__mouseX
    },
    get_mouseY: function() {
        return this.__mouseY
    },
    get_quality: function() {
        return this.__quality
    },
    set_rotation: function(a) {
        return 0
    },
    get_scaleMode: function() {
        return this.__scaleMode
    },
    set_scaleMode: function(a) {
        a !=
            this.__scaleMode && (this.__scaleMode = a, this.__resize());
        return a
    },
    set_scaleX: function(a) {
        return 0
    },
    set_scaleY: function(a) {
        return 0
    },
    get_tabEnabled: function() {
        return !1
    },
    get_tabIndex: function() {
        return -1
    },
    set_transform: function(a) {
        return this.get_transform()
    },
    set_width: function(a) {
        return this.get_width()
    },
    set_x: function(a) {
        return 0
    },
    set_y: function(a) {
        return 0
    },
    __class__: Lg,
    __properties__: v(kb.prototype.__properties__, {
        set_color: "set_color",
        set_scaleMode: "set_scaleMode",
        get_scaleMode: "get_scaleMode",
        get_quality: "get_quality",
        get_fullScreenSourceRect: "get_fullScreenSourceRect",
        get_frameRate: "get_frameRate",
        set_focus: "set_focus",
        get_focus: "get_focus"
    })
});
var Zf = function(a) {
    oa.call(this);
    this.__stage = a;
    this.__height = 0;
    this.__projectionTransform = new wj;
    this.__renderTransform = new wj;
    this.__y = this.__x = this.__width = 0;
    this.visible = !0;
    0 < a.stageWidth && 0 < a.stageHeight && this.__resize(a.stageWidth, a.stageHeight)
};
g["openfl.display.Stage3D"] = Zf;
Zf.__name__ = "openfl.display.Stage3D";
Zf.__super__ = oa;
Zf.prototype = v(oa.prototype, {
    __createContext: function() {
        var a =
            this.__stage,
            b = a.__renderer;
        if ("cairo" == b.__type || "canvas" == b.__type) this.__dispatchError();
        else if ("opengl" == b.__type) this.context3D = new ub(a, a.context3D.__contextState, this), this.__dispatchCreate();
        else if ("dom" == b.__type)
            if (null == a.context3D) {
                this.__canvas = window.document.createElement("canvas");
                this.__canvas.width = a.stageWidth;
                this.__canvas.height = a.stageHeight;
                var c = a.window.context.attributes,
                    d = Object.prototype.hasOwnProperty.call(c, "background") && null == c.background,
                    f = Object.prototype.hasOwnProperty.call(c,
                        "colorDepth") ? c.colorDepth : 32;
                c = Object.prototype.hasOwnProperty.call(c, "antialiasing") && 0 < c.antialiasing;
                this.__webgl = dl.getContextWebGL(this.__canvas, {
                    alpha: d || 16 < f,
                    antialias: c,
                    depth: !0,
                    premultipliedAlpha: !0,
                    stencil: !0,
                    preserveDrawingBuffer: !1
                });
                null != this.__webgl && (null == Xe.context && (Xe.context = this.__webgl, Xe.type = "webgl", Xe.version = 1), a.context3D = new ub(a), a.context3D.configureBackBuffer(a.window.__width, a.window.__height, 0, !0, !0, !0), a.context3D.present(), b.element.appendChild(this.__canvas),
                    this.__style = this.__canvas.style, this.__style.setProperty("position", "absolute", null), this.__style.setProperty("top", "0", null), this.__style.setProperty("left", "0", null), this.__style.setProperty(b.__transformOriginProperty, "0 0 0", null), this.__style.setProperty("z-index", "-1", null));
                null != a.context3D && (this.context3D = new ub(a, a.context3D.__contextState, this));
                this.__dispatchCreate()
            } else this.__dispatchError()
    },
    __dispatchError: function() {
        this.__contextRequested = !1;
        this.dispatchEvent(new Xd("error",
            !1, !1, "Context3D not available"))
    },
    __dispatchCreate: function() {
        this.__contextRequested && (this.__contextRequested = !1, this.dispatchEvent(new wa("context3DCreate")))
    },
    __lostContext: function() {
        this.__contextLost = !0;
        null != this.context3D && (this.context3D.__dispose(), this.__contextRequested = !0)
    },
    __resize: function(a, b) {
        if (a != this.__width || b != this.__height) null != this.__canvas && (this.__canvas.width = a, this.__canvas.height = b), this.__projectionTransform.copyRawDataFrom(la.toFloatVector(null, null, null, [2 / (0 < a ?
            a : 1), 0, 0, 0, 0, -2 / (0 < b ? b : 1), 0, 0, 0, 0, -.001, 0, -1, 1, 0, 1])), this.__renderTransform.identity(), this.__renderTransform.appendTranslation(this.__x, this.__y, 0), this.__renderTransform.append(this.__projectionTransform), this.__width = a, this.__height = b
    },
    __restoreContext: function() {
        this.__contextLost = !1;
        this.__createContext()
    },
    get_x: function() {
        return this.__x
    },
    get_y: function() {
        return this.__y
    },
    __class__: Zf,
    __properties__: {
        get_y: "get_y",
        get_x: "get_x"
    }
});
var Ah = function(a, b, c, d, f, h, k, n) {
    null == n && (n = 0);
    null == k && (k =
        0);
    null == h && (h = 0);
    null == f && (f = 1);
    null == d && (d = 1);
    null == c && (c = 0);
    null == b && (b = 0);
    null == a && (a = 0);
    this.__id = a;
    this.__matrix = new ua;
    0 != b && this.set_x(b);
    0 != c && this.set_y(c);
    1 != d && this.set_scaleX(d);
    1 != f && this.set_scaleY(f);
    0 != h && this.set_rotation(h);
    this.__dirty = !0;
    this.__length = 0;
    this.__originX = k;
    this.__originY = n;
    this.__alpha = 1;
    this.__blendMode = null;
    this.__visible = !0
};
g["openfl.display.Tile"] = Ah;
Ah.__name__ = "openfl.display.Tile";
Ah.prototype = {
    getBounds: function(a) {
        var b = new na;
        this.__findTileRect(b);
        var c =
            ua.__pool.get();
        if (null != a && a != this) {
            c.copyFrom(this.__getWorldTransform());
            var d = ua.__pool.get();
            d.copyFrom(a.__getWorldTransform());
            d.invert();
            c.concat(d);
            ua.__pool.release(d)
        } else c.identity();
        this.__getBounds(b, c);
        ua.__pool.release(c);
        return b
    },
    __getBounds: function(a, b) {
        a.__transform(a, b)
    },
    __findTileRect: function(a) {
        if (null == this.get_tileset())
            if (null != this.parent) {
                var b = this.parent.__findTileset();
                null == b ? a.setTo(0, 0, 0, 0) : (b = b.getRect(this.get_id()), null == b ? a.setTo(0, 0, 0, 0) : a.copyFrom(b))
            } else a.setTo(0,
                0, 0, 0);
        else a.copyFrom(this.get_tileset().getRect(this.get_id()));
        a.x = 0;
        a.y = 0
    },
    __findTileset: function() {
        return null != this.get_tileset() ? this.get_tileset() : this.parent instanceof Bh ? this.parent.get_tileset() : null == this.parent ? null : this.parent.__findTileset()
    },
    __getWorldTransform: function() {
        var a = this.get_matrix().clone();
        null != this.parent && a.concat(this.parent.__getWorldTransform());
        return a
    },
    __setRenderDirty: function() {
        this.__dirty || (this.__dirty = !0, null != this.parent && this.parent.__setRenderDirty())
    },
    get_alpha: function() {
        return this.__alpha
    },
    set_alpha: function(a) {
        a != this.__alpha && (this.__alpha = a, this.__setRenderDirty());
        return a
    },
    get_colorTransform: function() {
        return this.__colorTransform
    },
    get_height: function() {
        var a = na.__pool.get();
        this.__findTileRect(a);
        this.__getBounds(a, this.get_matrix());
        var b = a.height;
        na.__pool.release(a);
        return b
    },
    set_height: function(a) {
        var b = na.__pool.get();
        this.__findTileRect(b);
        0 != b.height && this.set_scaleY(a / b.height);
        na.__pool.release(b);
        return a
    },
    get_id: function() {
        return this.__id
    },
    get_matrix: function() {
        return this.__matrix
    },
    get_originX: function() {
        return this.__originX
    },
    set_originX: function(a) {
        a != this.__originX && (this.__originX = a, this.__setRenderDirty());
        return a
    },
    get_originY: function() {
        return this.__originY
    },
    set_originY: function(a) {
        a != this.__originY && (this.__originY = a, this.__setRenderDirty());
        return a
    },
    get_rotation: function() {
        if (null == this.__rotation)
            if (0 == this.__matrix.b && 0 == this.__matrix.c) this.__rotationSine = this.__rotation = 0, this.__rotationCosine = 1;
            else {
                var a = Math.atan2(this.__matrix.d,
                    this.__matrix.c) - Math.PI / 2;
                this.__rotation = 180 / Math.PI * a;
                this.__rotationSine = Math.sin(a);
                this.__rotationCosine = Math.cos(a)
            } return this.__rotation
    },
    set_rotation: function(a) {
        if (a != this.__rotation) {
            this.__rotation = a;
            var b = Math.PI / 180 * a;
            this.__rotationSine = Math.sin(b);
            this.__rotationCosine = Math.cos(b);
            b = this.get_scaleX();
            var c = this.get_scaleY();
            this.__matrix.a = this.__rotationCosine * b;
            this.__matrix.b = this.__rotationSine * b;
            this.__matrix.c = -this.__rotationSine * c;
            this.__matrix.d = this.__rotationCosine * c;
            this.__setRenderDirty()
        }
        return a
    },
    get_scaleX: function() {
        null == this.__scaleX && (0 == this.get_matrix().b ? this.__scaleX = this.__matrix.a : this.__scaleX = Math.sqrt(this.__matrix.a * this.__matrix.a + this.__matrix.b * this.__matrix.b));
        return this.__scaleX
    },
    set_scaleX: function(a) {
        if (a != this.__scaleX) {
            this.__scaleX = a;
            if (0 == this.__matrix.b) this.__matrix.a = a;
            else {
                this.get_rotation();
                var b = this.__rotationSine * a;
                this.__matrix.a = this.__rotationCosine * a;
                this.__matrix.b = b
            }
            this.__setRenderDirty()
        }
        return a
    },
    get_scaleY: function() {
        null == this.__scaleY && (this.__scaleY =
            0 == this.__matrix.c ? this.get_matrix().d : Math.sqrt(this.__matrix.c * this.__matrix.c + this.__matrix.d * this.__matrix.d));
        return this.__scaleY
    },
    set_scaleY: function(a) {
        if (a != this.__scaleY) {
            this.__scaleY = a;
            if (0 == this.__matrix.c) this.__matrix.d = a;
            else {
                this.get_rotation();
                var b = this.__rotationCosine * a;
                this.__matrix.c = -this.__rotationSine * a;
                this.__matrix.d = b
            }
            this.__setRenderDirty()
        }
        return a
    },
    get_shader: function() {
        return this.__shader
    },
    get_tileset: function() {
        return this.__tileset
    },
    set_tileset: function(a) {
        a != this.__tileset &&
            (this.__tileset = a, this.__setRenderDirty());
        return a
    },
    get_visible: function() {
        return this.__visible
    },
    get_width: function() {
        var a = na.__pool.get();
        this.__findTileRect(a);
        this.__getBounds(a, this.get_matrix());
        var b = a.width;
        na.__pool.release(a);
        return b
    },
    set_width: function(a) {
        var b = na.__pool.get();
        this.__findTileRect(b);
        0 != b.width && this.set_scaleX(a / b.width);
        na.__pool.release(b);
        return a
    },
    get_x: function() {
        return this.__matrix.tx
    },
    set_x: function(a) {
        a != this.__matrix.tx && (this.__matrix.tx = a, this.__setRenderDirty());
        return a
    },
    get_y: function() {
        return this.__matrix.ty
    },
    set_y: function(a) {
        a != this.__matrix.ty && (this.__matrix.ty = a, this.__setRenderDirty());
        return a
    },
    __class__: Ah,
    __properties__: {
        set_y: "set_y",
        get_y: "get_y",
        set_x: "set_x",
        get_x: "get_x",
        set_width: "set_width",
        get_width: "get_width",
        get_visible: "get_visible",
        set_tileset: "set_tileset",
        get_tileset: "get_tileset",
        get_shader: "get_shader",
        set_scaleY: "set_scaleY",
        get_scaleY: "get_scaleY",
        set_scaleX: "set_scaleX",
        get_scaleX: "get_scaleX",
        set_rotation: "set_rotation",
        get_rotation: "get_rotation",
        set_originY: "set_originY",
        get_originY: "get_originY",
        set_originX: "set_originX",
        get_originX: "get_originX",
        get_matrix: "get_matrix",
        get_id: "get_id",
        set_height: "set_height",
        get_height: "get_height",
        get_colorTransform: "get_colorTransform",
        set_alpha: "set_alpha",
        get_alpha: "get_alpha"
    }
};
var Ch = function(a, b, c, d, f, h, k) {
    null == k && (k = 0);
    null == h && (h = 0);
    null == f && (f = 0);
    null == d && (d = 1);
    null == c && (c = 1);
    null == b && (b = 0);
    null == a && (a = 0);
    Ah.call(this, -1, a, b, c, d, f, h, k);
    this.__tiles = [];
    this.__length =
        0
};
g["openfl.display.TileContainer"] = Ch;
Ch.__name__ = "openfl.display.TileContainer";
Ch.__interfaces__ = [Gk];
Ch.__super__ = Ah;
Ch.prototype = v(Ah.prototype, {
    getBounds: function(a) {
        for (var b = new na, c, d = 0, f = this.__tiles; d < f.length;) c = f[d], ++d, c = c.getBounds(a), b.__expand(c.x, c.y, c.width, c.height);
        return b
    },
    get_height: function() {
        for (var a = na.__pool.get(), b, c = 0, d = this.__tiles; c < d.length;) b = d[c], ++c, b = b.getBounds(this), a.__expand(b.x, b.y, b.width, b.height);
        this.__getBounds(a, this.get_matrix());
        c = a.height;
        na.__pool.release(a);
        return c
    },
    set_height: function(a) {
        for (var b = na.__pool.get(), c, d = 0, f = this.__tiles; d < f.length;) c = f[d], ++d, c = c.getBounds(this), b.__expand(c.x, c.y, c.width, c.height);
        0 != b.height && this.set_scaleY(a / b.height);
        na.__pool.release(b);
        return a
    },
    get_width: function() {
        for (var a = na.__pool.get(), b, c = 0, d = this.__tiles; c < d.length;) b = d[c], ++c, b = b.getBounds(this), a.__expand(b.x, b.y, b.width, b.height);
        this.__getBounds(a, this.get_matrix());
        c = a.width;
        na.__pool.release(a);
        return c
    },
    set_width: function(a) {
        for (var b = na.__pool.get(),
                c, d = 0, f = this.__tiles; d < f.length;) c = f[d], ++d, c = c.getBounds(this), b.__expand(c.x, c.y, c.width, c.height);
        0 != b.width && this.set_scaleX(a / b.width);
        na.__pool.release(b);
        return a
    },
    __class__: Ch
});
var Bh = function(a, b, c, d) {
    null == d && (d = !0);
    S.call(this);
    this.__drawableType = 9;
    this.__tileset = c;
    this.smoothing = d;
    this.tileColorTransformEnabled = this.tileBlendModeEnabled = this.tileAlphaEnabled = !0;
    this.__group = new Ch;
    this.__group.set_tileset(c);
    this.__width = a;
    this.__height = b
};
g["openfl.display.Tilemap"] = Bh;
Bh.__name__ =
    "openfl.display.Tilemap";
Bh.__interfaces__ = [Gk];
Bh.__super__ = S;
Bh.prototype = v(S.prototype, {
    __enterFrame: function(a) {
        this.__group.__dirty && !this.__renderDirty && (this.__renderDirty = !0, this.__setParentRenderDirty())
    },
    __getBounds: function(a, b) {
        var c = na.__pool.get();
        c.setTo(0, 0, this.__width, this.__height);
        c.__transform(c, b);
        a.__expand(c.x, c.y, c.width, c.height);
        na.__pool.release(c)
    },
    __hitTest: function(a, b, c, d, f, h) {
        if (!h.get_visible() || this.__isMask || null != this.get_mask() && !this.get_mask().__hitTestMask(a,
                b)) return !1;
        this.__getRenderTransform();
        var k = this.__renderTransform,
            n = k.a * k.d - k.b * k.c;
        c = 0 == n ? -k.tx : 1 / n * (k.c * (k.ty - b) + k.d * (a - k.tx));
        k = this.__renderTransform;
        n = k.a * k.d - k.b * k.c;
        a = 0 == n ? -k.ty : 1 / n * (k.a * (b - k.ty) + k.b * (k.tx - a));
        return 0 < c && 0 < a && c <= this.__width && a <= this.__height ? (null == d || f || d.push(h), !0) : !1
    },
    get_height: function() {
        return this.__height * Math.abs(this.get_scaleY())
    },
    set_height: function(a) {
        this.__height = a | 0;
        return this.__height * Math.abs(this.get_scaleY())
    },
    get_width: function() {
        return this.__width *
            Math.abs(this.__scaleX)
    },
    set_width: function(a) {
        this.__width = a | 0;
        return this.__width * Math.abs(this.__scaleX)
    },
    __class__: Bh
});
var pl = function(a, b) {
    this.__bitmapData = a;
    this.rectData = la.toFloatVector(null);
    this.__data = [];
    if (null != b)
        for (a = 0; a < b.length;) {
            var c = b[a];
            ++a;
            this.addRect(c)
        }
};
g["openfl.display.Tileset"] = pl;
pl.__name__ = "openfl.display.Tileset";
pl.prototype = {
    addRect: function(a) {
        if (null == a) return -1;
        this.rectData.push(a.x);
        this.rectData.push(a.y);
        this.rectData.push(a.width);
        this.rectData.push(a.height);
        a = new xj(a);
        a.__update(this.__bitmapData);
        this.__data.push(a);
        return this.__data.length - 1
    },
    getRect: function(a) {
        return a < this.__data.length && 0 <= a ? new na(this.__data[a].x, this.__data[a].y, this.__data[a].width, this.__data[a].height) : null
    },
    __class__: pl
};
var xj = function(a) {
    null != a && (this.x = a.x | 0, this.y = a.y | 0, this.width = a.width | 0, this.height = a.height | 0)
};
g["openfl.display.TileData"] = xj;
xj.__name__ = "openfl.display.TileData";
xj.prototype = {
    __update: function(a) {
        if (null != a) {
            var b = a.width;
            a = a.height;
            this.__uvX =
                this.x / b;
            this.__uvY = this.y / a;
            this.__uvWidth = (this.x + this.width) / b;
            this.__uvHeight = (this.y + this.height) / a
        }
    },
    __class__: xj
};
var ql = function() {
    this.__totalFrames = this.__framesLoaded = 1;
    this.__currentLabels = [];
    this.__currentFrame = 1;
    this.__lastFrameUpdate = this.__lastFrameScriptEval = -1
};
g["openfl.display.Timeline"] = ql;
ql.__name__ = "openfl.display.Timeline";
ql.prototype = {
    enterFrame: function(a) {},
    __enterFrame: function(a) {
        if (this.__isPlaying) {
            a = this.__getNextFrame(a);
            if (this.__lastFrameScriptEval == a) return;
            if (null !=
                this.__frameScripts) {
                if (a < this.__currentFrame) {
                    if (!this.__evaluateFrameScripts(this.__totalFrames)) return;
                    this.__currentFrame = 1
                }
                if (!this.__evaluateFrameScripts(a)) return
            } else this.__currentFrame = a
        }
        this.__updateSymbol(this.__currentFrame)
    },
    __evaluateFrameScripts: function(a) {
        if (null == this.__frameScripts) return !0;
        var b = this.__currentFrame;
        for (a += 1; b < a;) {
            var c = b++;
            if (c != this.__lastFrameScriptEval && (this.__currentFrame = this.__lastFrameScriptEval = c, this.__frameScripts.h.hasOwnProperty(c) && (this.__updateSymbol(c),
                    (0, this.__frameScripts.h[c])(this.__scope), this.__currentFrame != c) || !this.__isPlaying)) return !1
        }
        return !0
    },
    __getNextFrame: function(a) {
        null != this.frameRate ? (this.__timeElapsed += a, a = this.__currentFrame + Math.floor(this.__timeElapsed / this.__frameTime), 1 > a && (a = 1), a > this.__totalFrames && (a = Math.floor((a - 1) % this.__totalFrames) + 1), this.__timeElapsed %= this.__frameTime) : (a = this.__currentFrame + 1, a > this.__totalFrames && (a = 1));
        return a
    },
    __goto: function(a) {
        1 > a ? a = 1 : a > this.__totalFrames && (a = this.__totalFrames);
        this.__lastFrameScriptEval = -1;
        this.__currentFrame = a;
        this.__updateSymbol(this.__currentFrame);
        this.__evaluateFrameScripts(this.__currentFrame)
    },
    __gotoAndStop: function(a, b) {
        this.__stop();
        this.__goto(this.__resolveFrameReference(a))
    },
    __stop: function() {
        this.__isPlaying = !1
    },
    __resolveFrameReference: function(a) {
        if ("number" == typeof a && (a | 0) === a) return a;
        if ("string" == typeof a) {
            for (var b = 0, c = this.__currentLabels; b < c.length;) {
                var d = c[b];
                ++b;
                if (d.name == a) return d.frame
            }
            throw new gg("Error #2109: Frame label " + a + " not found in scene.");
        }
        throw X.thrown("Invalid type for frame " + a.__name__);
    },
    __updateFrameLabel: function() {
        this.__currentFrameLabel = this.__currentLabel = null;
        for (var a = 0, b = this.__currentLabels; a < b.length;) {
            var c = b[a];
            ++a;
            if (c.frame < this.__currentFrame) this.__currentLabel = c.name;
            else if (c.frame == this.__currentFrame) this.__currentFrameLabel = this.__currentLabel = c.name;
            else break
        }
    },
    __updateSymbol: function(a) {
        this.__currentFrame != this.__lastFrameUpdate && (this.__updateFrameLabel(), this.enterFrame(a), this.__lastFrameUpdate = this.__currentFrame)
    },
    __class__: ql
};
var vh = function(a, b) {
    Hg.call(this, a, b);
    this.stage = new Lg(this, Object.prototype.hasOwnProperty.call(b.context, "background") ? b.context.background : 16777215);
    if (Object.prototype.hasOwnProperty.call(b, "parameters")) try {
        this.stage.get_loaderInfo().parameters = b.parameters
    } catch (c) {
        Ta.lastError = c
    }
    this.stage.__setLogicalSize(b.width, b.height);
    Object.prototype.hasOwnProperty.call(b, "resizable") && !b.resizable && this.stage.set_scaleMode(3);
    a.addModule(this.stage)
};
g["openfl.display.Window"] = vh;
vh.__name__ =
    "openfl.display.Window";
vh.__super__ = Hg;
vh.prototype = v(Hg.prototype, {
    close: function() {
        Hg.prototype.close.call(this);
        this.onClose.canceled || null == this.stage || (this.application.removeModule(this.stage), this.stage = null)
    },
    __class__: vh
});
var wh = function() {};
g["openfl.display._internal.CanvasBitmap"] = wh;
wh.__name__ = "openfl.display._internal.CanvasBitmap";
wh.renderDrawable = function(a, b) {
    b.__updateCacheBitmap(a, !1);
    null != a.__bitmapData && null != a.__bitmapData.image && (a.__imageVersion = a.__bitmapData.image.version);
    if (null == a.__cacheBitmap || a.__isCacheBitmapRender) {
        if (!(null == a.opaqueBackground && null == a.__graphics || !a.__renderable || (x = b.__getAlpha(a.__worldAlpha), 0 >= x))) {
            if (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height()) {
                b.__setBlendMode(a.__worldBlendMode);
                b.__pushMaskObject(a);
                w = b.context;
                b.setTransform(a.__renderTransform, w);
                var c = a.opaqueBackground;
                w.fillStyle = "rgb(" + (c >>> 16 & 255) + "," + (c >>> 8 & 255) + "," + (c & 255) + ")";
                w.fillRect(0, 0, a.get_width(), a.get_height());
                b.__popMaskObject(a)
            }
            if (null !=
                a.__graphics && a.__renderable && (x = b.__getAlpha(a.__worldAlpha), !(0 >= x))) {
                var d = a.__graphics;
                if (null != d) {
                    z.render(d, b);
                    var f = d.__width,
                        h = d.__height;
                    c = d.__canvas;
                    if (null != c && d.__visible && 1 <= f && 1 <= h) {
                        var k = d.__worldTransform;
                        w = b.context;
                        J = a.__scrollRect;
                        var n = a.__worldScale9Grid;
                        if (null == J || 0 < J.width && 0 < J.height) {
                            b.__setBlendMode(a.__worldBlendMode);
                            b.__pushMaskObject(a);
                            w.globalAlpha = x;
                            if (null != n && 0 == k.b && 0 == k.c) {
                                var p = b.__pixelRatio;
                                x = ua.__pool.get();
                                x.translate(k.tx, k.ty);
                                b.setTransform(x, w);
                                ua.__pool.release(x);
                                x = d.__bounds;
                                var g = d.__renderTransform.a / d.__bitmapScale,
                                    q = d.__renderTransform.d / d.__bitmapScale,
                                    m = g * k.a,
                                    u = q * k.d;
                                k = Math.max(1, Math.round(n.x * g));
                                d = Math.round(n.y * q);
                                J = Math.max(1, Math.round((x.get_right() - n.get_right()) * g));
                                var r = Math.round((x.get_bottom() - n.get_bottom()) * q);
                                g = Math.round(n.width * g);
                                n = Math.round(n.height * q);
                                q = Math.round(k / p);
                                var l = Math.round(d / p),
                                    D = Math.round(J / p);
                                p = Math.round(r / p);
                                m = x.width * m - q - D;
                                x = x.height * u - l - p;
                                b.applySmoothing(w, !1);
                                0 != g && 0 != n ? (w.drawImage(c, 0, 0, k, d, 0, 0, q, l), w.drawImage(c,
                                    k, 0, g, d, q, 0, m, l), w.drawImage(c, k + g, 0, J, d, q + m, 0, D, l), w.drawImage(c, 0, d, k, n, 0, l, q, x), w.drawImage(c, k, d, g, n, q, l, m, x), w.drawImage(c, k + g, d, J, n, q + m, l, D, x), w.drawImage(c, 0, d + n, k, r, 0, l + x, q, p), w.drawImage(c, k, d + n, g, r, q, l + x, m, p), w.drawImage(c, k + g, d + n, J, r, q + m, l + x, D, p)) : 0 == g && 0 != n ? (h = q + m + D, w.drawImage(c, 0, 0, f, d, 0, 0, h, l), w.drawImage(c, 0, d, f, n, 0, l, h, x), w.drawImage(c, 0, d + n, f, r, 0, l + x, h, p)) : 0 == n && 0 != g && (f = l + x + p, w.drawImage(c, 0, 0, k, h, 0, 0, q, f), w.drawImage(c, k, 0, g, h, q, 0, m, f), w.drawImage(c, k + g, 0, J, h, q + m, 0, D, f))
                            } else b.setTransform(k,
                                w), w.drawImage(c, 0, 0, f, h);
                            b.__popMaskObject(a)
                        }
                    }
                }
            }
        }
        a.__renderable && (x = b.__getAlpha(a.__worldAlpha), 0 < x && null != a.__bitmapData && a.__bitmapData.__isValid && a.__bitmapData.readable && (w = b.context, b.__setBlendMode(a.__worldBlendMode), b.__pushMaskObject(a, !1), Ka.convertToCanvas(a.__bitmapData.image), w.globalAlpha = x, J = a.__scrollRect, b.setTransform(a.__renderTransform, w), b.__allowSmoothing && a.smoothing || (w.imageSmoothingEnabled = !1), null == J ? w.drawImage(a.__bitmapData.image.get_src(), 0, 0, a.__bitmapData.image.width,
            a.__bitmapData.image.height) : w.drawImage(a.__bitmapData.image.get_src(), J.x, J.y, J.width, J.height, J.x, J.y, J.width, J.height), b.__allowSmoothing && a.smoothing || (w.imageSmoothingEnabled = !0), b.__popMaskObject(a, !1)))
    } else if (c = a.__cacheBitmap, c.__renderable) {
        var x = b.__getAlpha(c.__worldAlpha);
        if (0 < x && null != c.__bitmapData && c.__bitmapData.__isValid && c.__bitmapData.readable) {
            var w = b.context;
            b.__setBlendMode(c.__worldBlendMode);
            b.__pushMaskObject(c, !1);
            Ka.convertToCanvas(c.__bitmapData.image);
            w.globalAlpha =
                x;
            var J = c.__scrollRect;
            b.setTransform(c.__renderTransform, w);
            b.__allowSmoothing && c.smoothing || (w.imageSmoothingEnabled = !1);
            null == J ? w.drawImage(c.__bitmapData.image.get_src(), 0, 0, c.__bitmapData.image.width, c.__bitmapData.image.height) : w.drawImage(c.__bitmapData.image.get_src(), J.x, J.y, J.width, J.height, J.x, J.y, J.width, J.height);
            b.__allowSmoothing && c.smoothing || (w.imageSmoothingEnabled = !0);
            b.__popMaskObject(c, !1)
        }
    }
    b.__renderEvent(a)
};
wh.renderDrawableMask = function(a, b) {
    b.context.rect(0, 0, a.get_width(),
        a.get_height())
};
var cj = function() {};
g["openfl.display._internal.CanvasBitmapData"] = cj;
cj.__name__ = "openfl.display._internal.CanvasBitmapData";
cj.renderDrawable = function(a, b) {
    if (a.readable) {
        var c = a.image;
        c.type == Zc.DATA && Ka.convertToCanvas(c);
        var d = b.context;
        d.globalAlpha = 1;
        b.setTransform(a.__renderTransform, d);
        d.drawImage(c.get_src(), 0, 0, c.width, c.height)
    }
};
cj.renderDrawableMask = function(a, b) {};
var Uf = function() {};
g["openfl.display._internal.CanvasDisplayObject"] = Uf;
Uf.__name__ = "openfl.display._internal.CanvasDisplayObject";
Uf.renderDrawable = function(a, b) {
    if (null == a.get_mask() || 0 < a.get_mask().get_width() && 0 < a.get_mask().get_height())
        if (b.__updateCacheBitmap(a, !1), null != a.__cacheBitmap && !a.__isCacheBitmapRender) {
            var c = a.__cacheBitmap;
            if (c.__renderable) {
                var d = b.__getAlpha(c.__worldAlpha);
                if (0 < d && null != c.__bitmapData && c.__bitmapData.__isValid && c.__bitmapData.readable) {
                    var f = b.context;
                    b.__setBlendMode(c.__worldBlendMode);
                    b.__pushMaskObject(c, !1);
                    Ka.convertToCanvas(c.__bitmapData.image);
                    f.globalAlpha = d;
                    var h = c.__scrollRect;
                    b.setTransform(c.__renderTransform, f);
                    b.__allowSmoothing && c.smoothing || (f.imageSmoothingEnabled = !1);
                    null == h ? f.drawImage(c.__bitmapData.image.get_src(), 0, 0, c.__bitmapData.image.width, c.__bitmapData.image.height) : f.drawImage(c.__bitmapData.image.get_src(), h.x, h.y, h.width, h.height, h.x, h.y, h.width, h.height);
                    b.__allowSmoothing && c.smoothing || (f.imageSmoothingEnabled = !0);
                    b.__popMaskObject(c, !1)
                }
            }
        } else if (!(null == a.opaqueBackground && null == a.__graphics || !a.__renderable || (d = b.__getAlpha(a.__worldAlpha), 0 >=
            d || (null != a.opaqueBackground && !a.__isCacheBitmapRender && 0 < a.get_width() && 0 < a.get_height() && (b.__setBlendMode(a.__worldBlendMode), b.__pushMaskObject(a), f = b.context, b.setTransform(a.__renderTransform, f), c = a.opaqueBackground, f.fillStyle = "rgb(" + (c >>> 16 & 255) + "," + (c >>> 8 & 255) + "," + (c & 255) + ")", f.fillRect(0, 0, a.get_width(), a.get_height()), b.__popMaskObject(a)), null == a.__graphics || !a.__renderable || (d = b.__getAlpha(a.__worldAlpha), 0 >= d))))) {
        var k = a.__graphics;
        if (null != k) {
            z.render(k, b);
            var n = k.__width,
                p = k.__height;
            c = k.__canvas;
            if (null != c && k.__visible && 1 <= n && 1 <= p) {
                var g = k.__worldTransform;
                f = b.context;
                h = a.__scrollRect;
                var q = a.__worldScale9Grid;
                if (null == h || 0 < h.width && 0 < h.height) {
                    b.__setBlendMode(a.__worldBlendMode);
                    b.__pushMaskObject(a);
                    f.globalAlpha = d;
                    if (null != q && 0 == g.b && 0 == g.c) {
                        var m = b.__pixelRatio;
                        d = ua.__pool.get();
                        d.translate(g.tx, g.ty);
                        b.setTransform(d, f);
                        ua.__pool.release(d);
                        d = k.__bounds;
                        var u = k.__renderTransform.a / k.__bitmapScale,
                            r = k.__renderTransform.d / k.__bitmapScale,
                            l = u * g.a,
                            D = r * g.d;
                        g = Math.max(1, Math.round(q.x *
                            u));
                        k = Math.round(q.y * r);
                        h = Math.max(1, Math.round((d.get_right() - q.get_right()) * u));
                        var x = Math.round((d.get_bottom() - q.get_bottom()) * r);
                        u = Math.round(q.width * u);
                        q = Math.round(q.height * r);
                        r = Math.round(g / m);
                        var w = Math.round(k / m),
                            J = Math.round(h / m);
                        m = Math.round(x / m);
                        l = d.width * l - r - J;
                        d = d.height * D - w - m;
                        b.applySmoothing(f, !1);
                        0 != u && 0 != q ? (f.drawImage(c, 0, 0, g, k, 0, 0, r, w), f.drawImage(c, g, 0, u, k, r, 0, l, w), f.drawImage(c, g + u, 0, h, k, r + l, 0, J, w), f.drawImage(c, 0, k, g, q, 0, w, r, d), f.drawImage(c, g, k, u, q, r, w, l, d), f.drawImage(c, g +
                            u, k, h, q, r + l, w, J, d), f.drawImage(c, 0, k + q, g, x, 0, w + d, r, m), f.drawImage(c, g, k + q, u, x, r, w + d, l, m), f.drawImage(c, g + u, k + q, h, x, r + l, w + d, J, m)) : 0 == u && 0 != q ? (p = r + l + J, f.drawImage(c, 0, 0, n, k, 0, 0, p, w), f.drawImage(c, 0, k, n, q, 0, w, p, d), f.drawImage(c, 0, k + q, n, x, 0, w + d, p, m)) : 0 == q && 0 != u && (n = w + d + m, f.drawImage(c, 0, 0, g, p, 0, 0, r, n), f.drawImage(c, g, 0, u, p, r, 0, l, n), f.drawImage(c, g + u, 0, h, p, r + l, 0, J, n))
                    } else b.setTransform(g, f), f.drawImage(c, 0, 0, n, p);
                    b.__popMaskObject(a)
                }
            }
        }
    }
    b.__renderEvent(a)
};
Uf.renderDrawableMask = function(a, b) {
    null != a.__graphics &&
        z.renderMask(a.__graphics, b)
};
var dj = function() {};
g["openfl.display._internal.CanvasDisplayObjectContainer"] = dj;
dj.__name__ = "openfl.display._internal.CanvasDisplayObjectContainer";
dj.renderDrawable = function(a, b) {
    for (var c = a.__removedChildren.iterator(); c.hasNext();) {
        var d = c.next();
        null == d.stage && d.__cleanup()
    }
    a.__removedChildren.set_length(0);
    if (!(!a.__renderable || 0 >= a.__worldAlpha || null != a.get_mask() && (0 >= a.get_mask().get_width() || 0 >= a.get_mask().get_height())) && (Uf.renderDrawable(a, b), null == a.__cacheBitmap ||
            a.__isCacheBitmapRender)) {
        b.__pushMaskObject(a);
        if (null != b.__stage) {
            c = 0;
            for (d = a.__children; c < d.length;) {
                var f = d[c];
                ++c;
                b.__renderDrawable(f);
                f.__renderDirty = !1
            }
            a.__renderDirty = !1
        } else
            for (c = 0, d = a.__children; c < d.length;) f = d[c], ++c, b.__renderDrawable(f);
        b.__popMaskObject(a)
