/*
 * mfcg.js - Split Module 1/6: $jscomp Polyfills
 * Google Closure Compiler runtime polyfills for ES6+ features
 * Original bundled comment: howler.js v2.2.4 | (c) 2013-2020, James Simpson of GoldFire Studios | MIT License | howlerjs.com  Spatial Plugin  @source http://purl.eligrey.com/github/FileSaver.js/blob/master/FileSaver.js
 */
var $jscomp = $jscomp || {};
$jscomp.scope = {};
$jscomp.arrayIteratorImpl = function(A) {
    var t = 0;
    return function() {
        return t < A.length ? {
            done: !1,
            value: A[t++]
        } : {
            done: !0
        }
    }
};
$jscomp.arrayIterator = function(A) {
    return {
        next: $jscomp.arrayIteratorImpl(A)
    }
};
$jscomp.ASSUME_ES5 = !1;
$jscomp.ASSUME_NO_NATIVE_MAP = !1;
$jscomp.ASSUME_NO_NATIVE_SET = !1;
$jscomp.SIMPLE_FROUND_POLYFILL = !1;
$jscomp.defineProperty = $jscomp.ASSUME_ES5 || "function" == typeof Object.defineProperties ? Object.defineProperty : function(A, t, E) {
    A != Array.prototype && A != Object.prototype && (A[t] = E.value)
};
$jscomp.getGlobal = function(A) {
    A = ["object" == typeof window && window, "object" == typeof self && self, "object" == typeof global && global, A];
    for (var t = 0; t < A.length; ++t) {
        var E = A[t];
        if (E && E.Math == Math) return E
    }
    throw Error("Cannot find global object");
};
$jscomp.global = $jscomp.getGlobal(this);
$jscomp.SYMBOL_PREFIX = "jscomp_symbol_";
$jscomp.initSymbol = function() {
    $jscomp.initSymbol = function() {};
    $jscomp.global.Symbol || ($jscomp.global.Symbol = $jscomp.Symbol)
};
$jscomp.SymbolClass = function(A, t) {
    this.$jscomp$symbol$id_ = A;
    $jscomp.defineProperty(this, "description", {
        configurable: !0,
        writable: !0,
        value: t
    })
};
$jscomp.SymbolClass.prototype.toString = function() {
    return this.$jscomp$symbol$id_
};
$jscomp.Symbol = function() {
    function A(E) {
        if (this instanceof A) throw new TypeError("Symbol is not a constructor");
        return new $jscomp.SymbolClass($jscomp.SYMBOL_PREFIX + (E || "") + "_" + t++, E)
    }
    var t = 0;
    return A
}();
$jscomp.initSymbolIterator = function() {
    $jscomp.initSymbol();
    var A = $jscomp.global.Symbol.iterator;
    A || (A = $jscomp.global.Symbol.iterator = $jscomp.global.Symbol("Symbol.iterator"));
    "function" != typeof Array.prototype[A] && $jscomp.defineProperty(Array.prototype, A, {
        configurable: !0,
        writable: !0,
        value: function() {
            return $jscomp.iteratorPrototype($jscomp.arrayIteratorImpl(this))
        }
    });
    $jscomp.initSymbolIterator = function() {}
};
$jscomp.initSymbolAsyncIterator = function() {
    $jscomp.initSymbol();
    var A = $jscomp.global.Symbol.asyncIterator;
    A || (A = $jscomp.global.Symbol.asyncIterator = $jscomp.global.Symbol("Symbol.asyncIterator"));
    $jscomp.initSymbolAsyncIterator = function() {}
};
$jscomp.iteratorPrototype = function(A) {
    $jscomp.initSymbolIterator();
    A = {
        next: A
    };
    A[$jscomp.global.Symbol.iterator] = function() {
        return this
    };
    return A
};
$jscomp.iteratorFromArray = function(A, t) {
    $jscomp.initSymbolIterator();
    A instanceof String && (A += "");
    var E = 0,
        B = {
            next: function() {
                if (E < A.length) {
                    var L = E++;
                    return {
                        value: t(L, A[L]),
                        done: !1
                    }
                }
                B.next = function() {
                    return {
                        done: !0,
                        value: void 0
                    }
                };
                return B.next()
            }
        };
    B[Symbol.iterator] = function() {
        return B
    };
    return B
};
$jscomp.polyfill = function(A, t, E, B) {
    if (t) {
        E = $jscomp.global;
        A = A.split(".");
        for (B = 0; B < A.length - 1; B++) {
            var L = A[B];
            L in E || (E[L] = {});
            E = E[L]
        }
        A = A[A.length - 1];
        B = E[A];
        t = t(B);
        t != B && null != t && $jscomp.defineProperty(E, A, {
            configurable: !0,
            writable: !0,
            value: t
        })
    }
};
$jscomp.polyfill("Array.prototype.keys", function(A) {
    return A ? A : function() {
        return $jscomp.iteratorFromArray(this, function(t) {
            return t
        })
    }
}, "es6", "es3");
$jscomp.checkStringArgs = function(A, t, E) {
    if (null == A) throw new TypeError("The 'this' value for String.prototype." + E + " must not be null or undefined");
    if (t instanceof RegExp) throw new TypeError("First argument to String.prototype." + E + " must not be a regular expression");
    return A + ""
};
$jscomp.polyfill("String.prototype.endsWith", function(A) {
    return A ? A : function(t, E) {
        var B = $jscomp.checkStringArgs(this, t, "endsWith");
        t += "";
        void 0 === E && (E = B.length);
        E = Math.max(0, Math.min(E | 0, B.length));
        for (var L = t.length; 0 < L && 0 < E;)
            if (B[--E] != t[--L]) return !1;
        return 0 >= L
    }
}, "es6", "es3");
$jscomp.polyfill("String.fromCodePoint", function(A) {
    return A ? A : function(t) {
        for (var E = "", B = 0; B < arguments.length; B++) {
            var L = Number(arguments[B]);
            if (0 > L || 1114111 < L || L !== Math.floor(L)) throw new RangeError("invalid_code_point " + L);
            65535 >= L ? E += String.fromCharCode(L) : (L -= 65536, E += String.fromCharCode(L >>> 10 & 1023 | 55296), E += String.fromCharCode(L & 1023 | 56320))
        }
        return E
    }
}, "es6", "es3");
$jscomp.polyfill("Array.prototype.values", function(A) {
    return A ? A : function() {
        return $jscomp.iteratorFromArray(this, function(t, E) {
            return E
        })
    }
}, "es8", "es3");
$jscomp.polyfill("Array.prototype.fill", function(A) {
    return A ? A : function(t, E, B) {
        var L = this.length || 0;
        0 > E && (E = Math.max(0, L + E));
        if (null == B || B > L) B = L;
        B = Number(B);
        0 > B && (B = Math.max(0, L + B));
        for (E = Number(E || 0); E < B; E++) this[E] = t;
        return this
    }
}, "es6", "es3");
$jscomp.findInternal = function(A, t, E) {
    A instanceof String && (A = String(A));
    for (var B = A.length, L = 0; L < B; L++) {
        var M = A[L];
        if (t.call(E, M, L, A)) return {
            i: L,
            v: M
        }
    }
    return {
        i: -1,
        v: void 0
    }
};
$jscomp.polyfill("Array.prototype.find", function(A) {
    return A ? A : function(t, E) {
        return $jscomp.findInternal(this, t, E).v
    }
}, "es6", "es3");
$jscomp.underscoreProtoCanBeSet = function() {
    var A = {
            a: !0
        },
        t = {};
    try {
        return t.__proto__ = A, t.a
    } catch (E) {}
    return !1
};
$jscomp.setPrototypeOf = "function" == typeof Object.setPrototypeOf ? Object.setPrototypeOf : $jscomp.underscoreProtoCanBeSet() ? function(A, t) {
    A.__proto__ = t;
    if (A.__proto__ !== t) throw new TypeError(A + " is not extensible");
    return A
} : null;
$jscomp.polyfill("Object.setPrototypeOf", function(A) {
    return A || $jscomp.setPrototypeOf
}, "es6", "es5");
$jscomp.owns = function(A, t) {
    return Object.prototype.hasOwnProperty.call(A, t)
};
$jscomp.polyfill("Object.values", function(A) {
    return A ? A : function(t) {
        var E = [],
            B;
        for (B in t) $jscomp.owns(t, B) && E.push(t[B]);
        return E
    }
}, "es8", "es3");
$jscomp.polyfill("String.prototype.startsWith", function(A) {
    return A ? A : function(t, E) {
        var B = $jscomp.checkStringArgs(this, t, "startsWith");
        t += "";
        var L = B.length,
            A = t.length;
        E = Math.max(0, Math.min(E | 0, B.length));
        for (var aa = 0; aa < A && E < L;)
            if (B[E++] != t[aa++]) return !1;
        return aa >= A
    }
}, "es6", "es3");
$jscomp.polyfill("Object.is", function(A) {
    return A ? A : function(t, E) {
        return t === E ? 0 !== t || 1 / t === 1 / E : t !== t && E !== E
    }
}, "es6", "es3");
$jscomp.polyfill("Array.prototype.includes", function(A) {
    return A ? A : function(t, E) {
        var B = this;
        B instanceof String && (B = String(B));
        var L = B.length;
        E = E || 0;
        for (0 > E && (E = Math.max(E + L, 0)); E < L; E++) {
            var A = B[E];
            if (A === t || Object.is(A, t)) return !0
        }
        return !1
    }
}, "es7", "es3");
$jscomp.polyfill("String.prototype.includes", function(A) {
    return A ? A : function(t, E) {
        return -1 !== $jscomp.checkStringArgs(this, t, "includes").indexOf(t, E || 0)
    }
}, "es6", "es3");
