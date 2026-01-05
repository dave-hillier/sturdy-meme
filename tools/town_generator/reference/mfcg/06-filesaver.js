/*
 * mfcg.js - Split Module 6/6: FileSaver.js
 * Client-side file saving library
 * @source http://purl.eligrey.com/github/FileSaver.js/blob/master/FileSaver.js
 */
if ("undefined" === typeof self || !self.constructor.name.includes("Worker")) {
    var saveAs = saveAs || function(A) {
        if (!("undefined" === typeof A || "undefined" !== typeof navigator && /MSIE [1-9]\./.test(navigator.userAgent))) {
            var t = A.document.createElementNS("http://www.w3.org/1999/xhtml", "a"),
                E = "download" in t,
                B = /constructor/i.test(A.HTMLElement) || A.safari,
                L = /CriOS\/[\d]+/.test(navigator.userAgent),
                M = function(g) {
                    (A.setImmediate || A.setTimeout)(function() {
                        throw g;
                    }, 0)
                },
                aa = function(g) {
                    setTimeout(function() {
                        "string" ===
                        typeof g ? (A.URL || A.webkitURL || A).revokeObjectURL(g) : g.remove()
                    }, 4E4)
                },
                v = function(g) {
                    return /^\s*(?:text\/\S*|application\/xml|\S*\/\S*\+xml)\s*;.*charset\s*=\s*utf-8/i.test(g.type) ? new Blob([String.fromCharCode(65279), g], {
                        type: g.type
                    }) : g
                },
                ea = function(g, l, y) {
                    y || (g = v(g));
                    var r = this,
                        ea = "application/octet-stream" === g.type,
                        sa = function() {
                            var g = ["writestart", "progress", "write", "writeend"];
                            g = [].concat(g);
                            for (var l = g.length; l--;) {
                                var t = r["on" + g[l]];
                                if ("function" === typeof t) try {
                                    t.call(r, r)
                                } catch (oa) {
                                    M(oa)
                                }
                            }
                        };
                    r.readyState = r.INIT;
                    if (E) {
                        var $a = (A.URL || A.webkitURL || A).createObjectURL(g);
                        setTimeout(function() {
                            t.href = $a;
                            t.download = l;
                            var g = new MouseEvent("click");
                            t.dispatchEvent(g);
                            sa();
                            aa($a);
                            r.readyState = r.DONE
                        })
                    } else(function() {
                        if ((L || ea && B) && A.FileReader) {
                            var l = new FileReader;
                            l.onloadend = function() {
                                var g = L ? l.result : l.result.replace(/^data:[^;]*;/, "data:attachment/file;");
                                A.open(g, "_blank") || (A.location.href = g);
                                r.readyState = r.DONE;
                                sa()
                            };
                            l.readAsDataURL(g);
                            r.readyState = r.INIT
                        } else $a || ($a = (A.URL || A.webkitURL ||
                            A).createObjectURL(g)), ea ? A.location.href = $a : A.open($a, "_blank") || (A.location.href = $a), r.readyState = r.DONE, sa(), aa($a)
                    })()
                },
                l = ea.prototype;
            if ("undefined" !== typeof navigator && navigator.msSaveOrOpenBlob) return function(g, l, t) {
                l = l || g.name || "download";
                t || (g = v(g));
                return navigator.msSaveOrOpenBlob(g, l)
            };
            l.abort = function() {};
            l.readyState = l.INIT = 0;
            l.WRITING = 1;
            l.DONE = 2;
            l.error = l.onwritestart = l.onprogress = l.onwrite = l.onabort = l.onerror = l.onwriteend = null;
            return function(g, l, t) {
                return new ea(g, l || g.name || "download",
                    t)
            }
        }
    }("undefined" !== typeof self && self || "undefined" !== typeof window && window || this.content);
    "undefined" !== typeof module && module.exports ? module.exports.saveAs = saveAs : "undefined" !== typeof define && null !== define && null !== define.amd && define("FileSaver.js", function() {
        return saveAs
    })
}
"function" === typeof define && define.__amd && (define.amd = define.__amd, delete define.__amd);