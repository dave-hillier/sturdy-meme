/*
 * mfcg.js - Split Module 3/6: Howler.js
 * Audio library for the web - howler.js v2.2.4
 * (c) 2013-2020, James Simpson of GoldFire Studios | MIT License
 * https://howlerjs.com
 */
"undefined" !== typeof self && self.constructor.name.includes("Worker") || (! function() {
    var A = function() {
        this.init()
    };
    A.prototype = {
        init: function() {
            var g = this || t;
            return g._counter = 1E3, g._html5AudioPool = [], g.html5PoolSize = 10, g._codecs = {}, g._howls = [], g._muted = !1, g._volume = 1, g._canPlayEvent = "canplaythrough", g._navigator = "undefined" != typeof window && window.navigator ? window.navigator : null, g.masterGain = null, g.noAudio = !1, g.usingWebAudio = !0, g.autoSuspend = !0, g.ctx = null, g.autoUnlock = !0, g._setup(), g
        },
        volume: function(g) {
            var r =
                this || t;
            if (g = parseFloat(g), r.ctx || l(), void 0 !== g && 0 <= g && 1 >= g) {
                if (r._volume = g, r._muted) return r;
                r.usingWebAudio && r.masterGain.gain.setValueAtTime(g, t.ctx.currentTime);
                for (var y = 0; y < r._howls.length; y++)
                    if (!r._howls[y]._webAudio)
                        for (var v = r._howls[y]._getSoundIds(), E = 0; E < v.length; E++) {
                            var sa = r._howls[y]._soundById(v[E]);
                            sa && sa._node && (sa._node.volume = sa._volume * g)
                        }
                return r
            }
            return r._volume
        },
        mute: function(g) {
            var r = this || t;
            r.ctx || l();
            r._muted = g;
            r.usingWebAudio && r.masterGain.gain.setValueAtTime(g ? 0 : r._volume,
                t.ctx.currentTime);
            for (var y = 0; y < r._howls.length; y++)
                if (!r._howls[y]._webAudio)
                    for (var v = r._howls[y]._getSoundIds(), E = 0; E < v.length; E++) {
                        var sa = r._howls[y]._soundById(v[E]);
                        sa && sa._node && (sa._node.muted = !!g || sa._muted)
                    }
            return r
        },
        stop: function() {
            for (var g = this || t, r = 0; r < g._howls.length; r++) g._howls[r].stop();
            return g
        },
        unload: function() {
            for (var g = this || t, r = g._howls.length - 1; 0 <= r; r--) g._howls[r].unload();
            return g.usingWebAudio && g.ctx && void 0 !== g.ctx.close && (g.ctx.close(), g.ctx = null, l()), g
        },
        codecs: function(g) {
            return (this ||
                t)._codecs[g.replace(/^x-/, "")]
        },
        _setup: function() {
            var g = this || t;
            if (g.state = g.ctx ? g.ctx.state || "suspended" : "suspended", g._autoSuspend(), !g.usingWebAudio)
                if ("undefined" != typeof Audio) try {
                    var r = new Audio;
                    void 0 === r.oncanplaythrough && (g._canPlayEvent = "canplay")
                } catch (y) {
                    g.noAudio = !0
                } else g.noAudio = !0;
            try {
                r = new Audio, r.muted && (g.noAudio = !0)
            } catch (y) {}
            return g.noAudio || g._setupCodecs(), g
        },
        _setupCodecs: function() {
            var g = this || t,
                r = null;
            try {
                r = "undefined" != typeof Audio ? new Audio : null
            } catch ($a) {
                return g
            }
            if (!r ||
                "function" != typeof r.canPlayType) return g;
            var l = r.canPlayType("audio/mpeg;").replace(/^no$/, ""),
                v = g._navigator ? g._navigator.userAgent : "",
                E = v.match(/OPR\/(\d+)/g);
            E = E && 33 > parseInt(E[0].split("/")[1], 10);
            var sa = -1 !== v.indexOf("Safari") && -1 === v.indexOf("Chrome");
            v = v.match(/Version\/(.*?) /);
            v = sa && v && 15 > parseInt(v[1], 10);
            return g._codecs = {
                mp3: !(E || !l && !r.canPlayType("audio/mp3;").replace(/^no$/, "")),
                mpeg: !!l,
                opus: !!r.canPlayType('audio/ogg; codecs="opus"').replace(/^no$/, ""),
                ogg: !!r.canPlayType('audio/ogg; codecs="vorbis"').replace(/^no$/,
                    ""),
                oga: !!r.canPlayType('audio/ogg; codecs="vorbis"').replace(/^no$/, ""),
                wav: !!(r.canPlayType('audio/wav; codecs="1"') || r.canPlayType("audio/wav")).replace(/^no$/, ""),
                aac: !!r.canPlayType("audio/aac;").replace(/^no$/, ""),
                caf: !!r.canPlayType("audio/x-caf;").replace(/^no$/, ""),
                m4a: !!(r.canPlayType("audio/x-m4a;") || r.canPlayType("audio/m4a;") || r.canPlayType("audio/aac;")).replace(/^no$/, ""),
                m4b: !!(r.canPlayType("audio/x-m4b;") || r.canPlayType("audio/m4b;") || r.canPlayType("audio/aac;")).replace(/^no$/,
                    ""),
                mp4: !!(r.canPlayType("audio/x-mp4;") || r.canPlayType("audio/mp4;") || r.canPlayType("audio/aac;")).replace(/^no$/, ""),
                weba: !(v || !r.canPlayType('audio/webm; codecs="vorbis"').replace(/^no$/, "")),
                webm: !(v || !r.canPlayType('audio/webm; codecs="vorbis"').replace(/^no$/, "")),
                dolby: !!r.canPlayType('audio/mp4; codecs="ec-3"').replace(/^no$/, ""),
                flac: !!(r.canPlayType("audio/x-flac;") || r.canPlayType("audio/flac;")).replace(/^no$/, "")
            }, g
        },
        _unlockAudio: function() {
            var g = this || t;
            if (!g._audioUnlocked && g.ctx) {
                g._audioUnlocked = !1;
                g.autoUnlock = !1;
                g._mobileUnloaded || 44100 === g.ctx.sampleRate || (g._mobileUnloaded = !0, g.unload());
                g._scratchBuffer = g.ctx.createBuffer(1, 1, 22050);
                var r = function(l) {
                    for (; g._html5AudioPool.length < g.html5PoolSize;) try {
                        var y = new Audio;
                        y._unlocked = !0;
                        g._releaseHtml5Audio(y)
                    } catch (Pa) {
                        g.noAudio = !0;
                        break
                    }
                    for (l = 0; l < g._howls.length; l++)
                        if (!g._howls[l]._webAudio) {
                            y = g._howls[l]._getSoundIds();
                            for (var t = 0; t < y.length; t++) {
                                var v = g._howls[l]._soundById(y[t]);
                                v && v._node && !v._node._unlocked && (v._node._unlocked = !0,
                                    v._node.load())
                            }
                        } g._autoResume();
                    var E = g.ctx.createBufferSource();
                    E.buffer = g._scratchBuffer;
                    E.connect(g.ctx.destination);
                    void 0 === E.start ? E.noteOn(0) : E.start(0);
                    "function" == typeof g.ctx.resume && g.ctx.resume();
                    E.onended = function() {
                        E.disconnect(0);
                        g._audioUnlocked = !0;
                        document.removeEventListener("touchstart", r, !0);
                        document.removeEventListener("touchend", r, !0);
                        document.removeEventListener("click", r, !0);
                        document.removeEventListener("keydown", r, !0);
                        for (var l = 0; l < g._howls.length; l++) g._howls[l]._emit("unlock")
                    }
                };
                return document.addEventListener("touchstart", r, !0), document.addEventListener("touchend", r, !0), document.addEventListener("click", r, !0), document.addEventListener("keydown", r, !0), g
            }
        },
        _obtainHtml5Audio: function() {
            var g = this || t;
            if (g._html5AudioPool.length) return g._html5AudioPool.pop();
            g = (new Audio).play();
            return g && "undefined" != typeof Promise && (g instanceof Promise || "function" == typeof g.then) && g.catch(function() {
                    console.warn("HTML5 Audio pool exhausted, returning potentially locked audio object.")
                }),
                new Audio
        },
        _releaseHtml5Audio: function(g) {
            var r = this || t;
            return g._unlocked && r._html5AudioPool.push(g), r
        },
        _autoSuspend: function() {
            var g = this;
            if (g.autoSuspend && g.ctx && void 0 !== g.ctx.suspend && t.usingWebAudio) {
                for (var r = 0; r < g._howls.length; r++)
                    if (g._howls[r]._webAudio)
                        for (var l = 0; l < g._howls[r]._sounds.length; l++)
                            if (!g._howls[r]._sounds[l]._paused) return g;
                return g._suspendTimer && clearTimeout(g._suspendTimer), g._suspendTimer = setTimeout(function() {
                    if (g.autoSuspend) {
                        g._suspendTimer = null;
                        g.state = "suspending";
                        var r = function() {
                            g.state = "suspended";
                            g._resumeAfterSuspend && (delete g._resumeAfterSuspend, g._autoResume())
                        };
                        g.ctx.suspend().then(r, r)
                    }
                }, 3E4), g
            }
        },
        _autoResume: function() {
            var g = this;
            if (g.ctx && void 0 !== g.ctx.resume && t.usingWebAudio) return "running" === g.state && "interrupted" !== g.ctx.state && g._suspendTimer ? (clearTimeout(g._suspendTimer), g._suspendTimer = null) : "suspended" === g.state || "running" === g.state && "interrupted" === g.ctx.state ? (g.ctx.resume().then(function() {
                    g.state = "running";
                    for (var r = 0; r < g._howls.length; r++) g._howls[r]._emit("resume")
                }),
                g._suspendTimer && (clearTimeout(g._suspendTimer), g._suspendTimer = null)) : "suspending" === g.state && (g._resumeAfterSuspend = !0), g
        }
    };
    var t = new A,
        E = function(g) {
            if (!g.src || 0 === g.src.length) return void console.error("An array of source files must be passed with any new Howl.");
            this.init(g)
        };
    E.prototype = {
        init: function(g) {
            var r = this;
            return t.ctx || l(), r._autoplay = g.autoplay || !1, r._format = "string" != typeof g.format ? g.format : [g.format], r._html5 = g.html5 || !1, r._muted = g.mute || !1, r._loop = g.loop || !1, r._pool = g.pool || 5,
                r._preload = "boolean" != typeof g.preload && "metadata" !== g.preload || g.preload, r._rate = g.rate || 1, r._sprite = g.sprite || {}, r._src = "string" != typeof g.src ? g.src : [g.src], r._volume = void 0 !== g.volume ? g.volume : 1, r._xhr = {
                    method: g.xhr && g.xhr.method ? g.xhr.method : "GET",
                    headers: g.xhr && g.xhr.headers ? g.xhr.headers : null,
                    withCredentials: !(!g.xhr || !g.xhr.withCredentials) && g.xhr.withCredentials
                }, r._duration = 0, r._state = "unloaded", r._sounds = [], r._endTimers = {}, r._queue = [], r._playLock = !1, r._onend = g.onend ? [{
                    fn: g.onend
                }] : [], r._onfade =
                g.onfade ? [{
                    fn: g.onfade
                }] : [], r._onload = g.onload ? [{
                    fn: g.onload
                }] : [], r._onloaderror = g.onloaderror ? [{
                    fn: g.onloaderror
                }] : [], r._onplayerror = g.onplayerror ? [{
                    fn: g.onplayerror
                }] : [], r._onpause = g.onpause ? [{
                    fn: g.onpause
                }] : [], r._onplay = g.onplay ? [{
                    fn: g.onplay
                }] : [], r._onstop = g.onstop ? [{
                    fn: g.onstop
                }] : [], r._onmute = g.onmute ? [{
                    fn: g.onmute
                }] : [], r._onvolume = g.onvolume ? [{
                    fn: g.onvolume
                }] : [], r._onrate = g.onrate ? [{
                    fn: g.onrate
                }] : [], r._onseek = g.onseek ? [{
                    fn: g.onseek
                }] : [], r._onunlock = g.onunlock ? [{
                    fn: g.onunlock
                }] : [], r._onresume = [], r._webAudio = t.usingWebAudio && !r._html5, void 0 !== t.ctx && t.ctx && t.autoUnlock && t._unlockAudio(), t._howls.push(r), r._autoplay && r._queue.push({
                    event: "play",
                    action: function() {
                        r.play()
                    }
                }), r._preload && "none" !== r._preload && r.load(), r
        },
        load: function() {
            var g = null;
            if (t.noAudio) return void this._emit("loaderror", null, "No audio support.");
            "string" == typeof this._src && (this._src = [this._src]);
            for (var r = 0; r < this._src.length; r++) {
                var l;
                if (this._format && this._format[r]) var v = this._format[r];
                else {
                    if ("string" != typeof(l =
                            this._src[r])) {
                        this._emit("loaderror", null, "Non-string found in selected audio sources - ignoring.");
                        continue
                    }(v = /^data:audio\/([^;,]+);/i.exec(l)) || (v = /\.([^.]+)$/.exec(l.split("?", 1)[0]));
                    v && (v = v[1].toLowerCase())
                }
                if (v || console.warn('No file extension was found. Consider using the "format" property or specify an extension.'), v && t.codecs(v)) {
                    g = this._src[r];
                    break
                }
            }
            return g ? (this._src = g, this._state = "loading", "https:" === window.location.protocol && "http:" === g.slice(0, 5) && (this._html5 = !0, this._webAudio = !1), new B(this), this._webAudio && M(this), this) : void this._emit("loaderror", null, "No codec support for selected audio sources.")
        },
        play: function(g, r) {
            var l = this,
                v = null;
            if ("number" == typeof g) v = g, g = null;
            else {
                if ("string" == typeof g && "loaded" === l._state && !l._sprite[g]) return null;
                if (void 0 === g && (g = "__default", !l._playLock)) {
                    for (var E = 0, sa = 0; sa < l._sounds.length; sa++) l._sounds[sa]._paused && !l._sounds[sa]._ended && (E++, v = l._sounds[sa]._id);
                    1 === E ? g = null : v = null
                }
            }
            var A = v ? l._soundById(v) : l._inactiveSound();
            if (!A) return null;
            if (v && !g && (g = A._sprite || "__default"), "loaded" !== l._state) {
                A._sprite = g;
                A._ended = !1;
                var B = A._id;
                return l._queue.push({
                    event: "play",
                    action: function() {
                        l.play(B)
                    }
                }), B
            }
            if (v && !A._paused) return r || l._loadQueue("play"), A._id;
            l._webAudio && t._autoResume();
            var L = Math.max(0, 0 < A._seek ? A._seek : l._sprite[g][0] / 1E3),
                aa = Math.max(0, (l._sprite[g][0] + l._sprite[g][1]) / 1E3 - L),
                ea = 1E3 * aa / Math.abs(A._rate),
                M = l._sprite[g][0] / 1E3,
                la = (l._sprite[g][0] + l._sprite[g][1]) / 1E3;
            A._sprite = g;
            A._ended = !1;
            var nb = function() {
                A._paused = !1;
                A._seek = L;
                A._start = M;
                A._stop = la;
                A._loop = !(!A._loop && !l._sprite[g][2])
            };
            if (L >= la) return void l._ended(A);
            var Y = A._node;
            if (l._webAudio) v = function() {
                l._playLock = !1;
                nb();
                l._refreshBuffer(A);
                Y.gain.setValueAtTime(A._muted || l._muted ? 0 : A._volume, t.ctx.currentTime);
                A._playStart = t.ctx.currentTime;
                void 0 === Y.bufferSource.start ? A._loop ? Y.bufferSource.noteGrainOn(0, L, 86400) : Y.bufferSource.noteGrainOn(0, L, aa) : A._loop ? Y.bufferSource.start(0, L, 86400) : Y.bufferSource.start(0, L, aa);
                ea !== 1 / 0 && (l._endTimers[A._id] = setTimeout(l._ended.bind(l,
                    A), ea));
                r || setTimeout(function() {
                    l._emit("play", A._id);
                    l._loadQueue()
                }, 0)
            }, "running" === t.state && "interrupted" !== t.ctx.state ? v() : (l._playLock = !0, l.once("resume", v), l._clearTimer(A._id));
            else {
                var pa = function() {
                    Y.currentTime = L;
                    Y.muted = A._muted || l._muted || t._muted || Y.muted;
                    Y.volume = A._volume * t.volume();
                    Y.playbackRate = A._rate;
                    try {
                        var y = Y.play();
                        if (y && "undefined" != typeof Promise && (y instanceof Promise || "function" == typeof y.then) ? (l._playLock = !0, nb(), y.then(function() {
                                l._playLock = !1;
                                Y._unlocked = !0;
                                r ? l._loadQueue() :
                                    l._emit("play", A._id)
                            }).catch(function() {
                                l._playLock = !1;
                                l._emit("playerror", A._id, "Playback was unable to start. This is most commonly an issue on mobile devices and Chrome where playback was not within a user interaction.");
                                A._ended = !0;
                                A._paused = !0
                            })) : r || (l._playLock = !1, nb(), l._emit("play", A._id)), Y.playbackRate = A._rate, Y.paused) return void l._emit("playerror", A._id, "Playback was unable to start. This is most commonly an issue on mobile devices and Chrome where playback was not within a user interaction.");
                        "__default" !== g || A._loop ? l._endTimers[A._id] = setTimeout(l._ended.bind(l, A), ea) : (l._endTimers[A._id] = function() {
                            l._ended(A);
                            Y.removeEventListener("ended", l._endTimers[A._id], !1)
                        }, Y.addEventListener("ended", l._endTimers[A._id], !1))
                    } catch (xa) {
                        l._emit("playerror", A._id, xa)
                    }
                };
                "data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAAABkYXRhAgAAAAEA" === Y.src && (Y.src = l._src, Y.load());
                v = window && window.ejecta || !Y.readyState && t._navigator.isCocoonJS;
                if (3 <= Y.readyState || v) pa();
                else {
                    l._playLock = !0;
                    l._state = "loading";
                    var ab = function() {
                        l._state = "loaded";
                        pa();
                        Y.removeEventListener(t._canPlayEvent, ab, !1)
                    };
                    Y.addEventListener(t._canPlayEvent, ab, !1);
                    l._clearTimer(A._id)
                }
            }
            return A._id
        },
        pause: function(g, r) {
            var l = this;
            if ("loaded" !== l._state || l._playLock) return l._queue.push({
                event: "pause",
                action: function() {
                    l.pause(g)
                }
            }), l;
            for (var t = l._getSoundIds(g), v = 0; v < t.length; v++) {
                l._clearTimer(t[v]);
                var A = l._soundById(t[v]);
                if (A && !A._paused && (A._seek = l.seek(t[v]), A._rateSeek = 0, A._paused = !0, l._stopFade(t[v]),
                        A._node))
                    if (l._webAudio) {
                        if (!A._node.bufferSource) continue;
                        void 0 === A._node.bufferSource.stop ? A._node.bufferSource.noteOff(0) : A._node.bufferSource.stop(0);
                        l._cleanBuffer(A._node)
                    } else isNaN(A._node.duration) && A._node.duration !== 1 / 0 || A._node.pause();
                r || l._emit("pause", A ? A._id : null)
            }
            return l
        },
        stop: function(g, r) {
            var l = this;
            if ("loaded" !== l._state || l._playLock) return l._queue.push({
                event: "stop",
                action: function() {
                    l.stop(g)
                }
            }), l;
            for (var t = l._getSoundIds(g), v = 0; v < t.length; v++) {
                l._clearTimer(t[v]);
                var A = l._soundById(t[v]);
                A && (A._seek = A._start || 0, A._rateSeek = 0, A._paused = !0, A._ended = !0, l._stopFade(t[v]), A._node && (l._webAudio ? A._node.bufferSource && (void 0 === A._node.bufferSource.stop ? A._node.bufferSource.noteOff(0) : A._node.bufferSource.stop(0), l._cleanBuffer(A._node)) : isNaN(A._node.duration) && A._node.duration !== 1 / 0 || (A._node.currentTime = A._start || 0, A._node.pause(), A._node.duration === 1 / 0 && l._clearSound(A._node))), r || l._emit("stop", A._id))
            }
            return l
        },
        mute: function(g, r) {
            var l = this;
            if ("loaded" !== l._state || l._playLock) return l._queue.push({
                event: "mute",
                action: function() {
                    l.mute(g, r)
                }
            }), l;
            if (void 0 === r) {
                if ("boolean" != typeof g) return l._muted;
                l._muted = g
            }
            for (var v = l._getSoundIds(r), A = 0; A < v.length; A++) {
                var E = l._soundById(v[A]);
                E && (E._muted = g, E._interval && l._stopFade(E._id), l._webAudio && E._node ? E._node.gain.setValueAtTime(g ? 0 : E._volume, t.ctx.currentTime) : E._node && (E._node.muted = !!t._muted || g), l._emit("mute", E._id))
            }
            return l
        },
        volume: function() {
            var g, r, l = this,
                v = arguments;
            if (0 === v.length) return l._volume;
            1 === v.length || 2 === v.length && void 0 === v[1] ? 0 <= l._getSoundIds().indexOf(v[0]) ?
                r = parseInt(v[0], 10) : g = parseFloat(v[0]) : 2 <= v.length && (g = parseFloat(v[0]), r = parseInt(v[1], 10));
            var A;
            if (!(void 0 !== g && 0 <= g && 1 >= g)) return A = r ? l._soundById(r) : l._sounds[0], A ? A._volume : 0;
            if ("loaded" !== l._state || l._playLock) return l._queue.push({
                event: "volume",
                action: function() {
                    l.volume.apply(l, v)
                }
            }), l;
            void 0 === r && (l._volume = g);
            r = l._getSoundIds(r);
            for (var E = 0; E < r.length; E++)(A = l._soundById(r[E])) && (A._volume = g, v[2] || l._stopFade(r[E]), l._webAudio && A._node && !A._muted ? A._node.gain.setValueAtTime(g, t.ctx.currentTime) :
                A._node && !A._muted && (A._node.volume = g * t.volume()), l._emit("volume", A._id));
            return l
        },
        fade: function(g, l, v, G) {
            var r = this;
            if ("loaded" !== r._state || r._playLock) return r._queue.push({
                event: "fade",
                action: function() {
                    r.fade(g, l, v, G)
                }
            }), r;
            g = Math.min(Math.max(0, parseFloat(g)), 1);
            l = Math.min(Math.max(0, parseFloat(l)), 1);
            v = parseFloat(v);
            r.volume(g, G);
            for (var y = r._getSoundIds(G), A = 0; A < y.length; A++) {
                var E = r._soundById(y[A]);
                if (E) {
                    if (G || r._stopFade(y[A]), r._webAudio && !E._muted) {
                        var B = t.ctx.currentTime,
                            L = B + v / 1E3;
                        E._volume =
                            g;
                        E._node.gain.setValueAtTime(g, B);
                        E._node.gain.linearRampToValueAtTime(l, L)
                    }
                    r._startFadeInterval(E, g, l, v, y[A], void 0 === G)
                }
            }
            return r
        },
        _startFadeInterval: function(g, l, v, t, A, E) {
            var r = this,
                y = l,
                G = v - l;
            A = Math.abs(G / .01);
            A = Math.max(4, 0 < A ? t / A : t);
            var B = Date.now();
            g._fadeTo = v;
            g._interval = setInterval(function() {
                var A = (Date.now() - B) / t;
                B = Date.now();
                y += G * A;
                y = Math.round(100 * y) / 100;
                y = 0 > G ? Math.max(v, y) : Math.min(v, y);
                r._webAudio ? g._volume = y : r.volume(y, g._id, !0);
                E && (r._volume = y);
                (v < l && y <= v || v > l && y >= v) && (clearInterval(g._interval),
                    g._interval = null, g._fadeTo = null, r.volume(v, g._id), r._emit("fade", g._id))
            }, A)
        },
        _stopFade: function(g) {
            var l = this._soundById(g);
            return l && l._interval && (this._webAudio && l._node.gain.cancelScheduledValues(t.ctx.currentTime), clearInterval(l._interval), l._interval = null, this.volume(l._fadeTo, g), l._fadeTo = null, this._emit("fade", g)), this
        },
        loop: function() {
            var g, l, v, t = arguments;
            if (0 === t.length) return this._loop;
            if (1 === t.length) {
                if ("boolean" != typeof t[0]) return !!(v = this._soundById(parseInt(t[0], 10))) && v._loop;
                this._loop = g = t[0]
            } else 2 === t.length && (g = t[0], l = parseInt(t[1], 10));
            l = this._getSoundIds(l);
            for (t = 0; t < l.length; t++)(v = this._soundById(l[t])) && (v._loop = g, this._webAudio && v._node && v._node.bufferSource && (v._node.bufferSource.loop = g, g && (v._node.bufferSource.loopStart = v._start || 0, v._node.bufferSource.loopEnd = v._stop, this.playing(l[t]) && (this.pause(l[t], !0), this.play(l[t], !0)))));
            return this
        },
        rate: function() {
            var g, l, v = this,
                A = arguments;
            0 === A.length ? l = v._sounds[0]._id : 1 === A.length ? 0 <= v._getSoundIds().indexOf(A[0]) ?
                l = parseInt(A[0], 10) : g = parseFloat(A[0]) : 2 === A.length && (g = parseFloat(A[0]), l = parseInt(A[1], 10));
            var E;
            if ("number" != typeof g) return E = v._soundById(l), E ? E._rate : v._rate;
            if ("loaded" !== v._state || v._playLock) return v._queue.push({
                event: "rate",
                action: function() {
                    v.rate.apply(v, A)
                }
            }), v;
            void 0 === l && (v._rate = g);
            l = v._getSoundIds(l);
            for (var B = 0; B < l.length; B++)
                if (E = v._soundById(l[B])) {
                    v.playing(l[B]) && (E._rateSeek = v.seek(l[B]), E._playStart = v._webAudio ? t.ctx.currentTime : E._playStart);
                    E._rate = g;
                    v._webAudio && E._node &&
                        E._node.bufferSource ? E._node.bufferSource.playbackRate.setValueAtTime(g, t.ctx.currentTime) : E._node && (E._node.playbackRate = g);
                    var L = v.seek(l[B]);
                    L = 1E3 * ((v._sprite[E._sprite][0] + v._sprite[E._sprite][1]) / 1E3 - L) / Math.abs(E._rate);
                    !v._endTimers[l[B]] && E._paused || (v._clearTimer(l[B]), v._endTimers[l[B]] = setTimeout(v._ended.bind(v, E), L));
                    v._emit("rate", E._id)
                } return v
        },
        seek: function() {
            var g, l, v = this,
                A = arguments;
            0 === A.length ? v._sounds.length && (l = v._sounds[0]._id) : 1 === A.length ? 0 <= v._getSoundIds().indexOf(A[0]) ?
                l = parseInt(A[0], 10) : v._sounds.length && (l = v._sounds[0]._id, g = parseFloat(A[0])) : 2 === A.length && (g = parseFloat(A[0]), l = parseInt(A[1], 10));
            if (void 0 === l) return 0;
            if ("number" == typeof g && ("loaded" !== v._state || v._playLock)) return v._queue.push({
                event: "seek",
                action: function() {
                    v.seek.apply(v, A)
                }
            }), v;
            var E = v._soundById(l);
            if (E) {
                if (!("number" == typeof g && 0 <= g)) return v._webAudio ? (g = v.playing(l) ? t.ctx.currentTime - E._playStart : 0, E._seek + ((E._rateSeek ? E._rateSeek - E._seek : 0) + g * Math.abs(E._rate))) : E._node.currentTime;
                var B = v.playing(l);
                B && v.pause(l, !0);
                E._seek = g;
                E._ended = !1;
                v._clearTimer(l);
                v._webAudio || !E._node || isNaN(E._node.duration) || (E._node.currentTime = g);
                var L = function() {
                    B && v.play(l, !0);
                    v._emit("seek", l)
                };
                if (B && !v._webAudio) {
                    var aa = function() {
                        v._playLock ? setTimeout(aa, 0) : L()
                    };
                    setTimeout(aa, 0)
                } else L()
            }
            return v
        },
        playing: function(g) {
            if ("number" == typeof g) return g = this._soundById(g), !!g && !g._paused;
            for (g = 0; g < this._sounds.length; g++)
                if (!this._sounds[g]._paused) return !0;
            return !1
        },
        duration: function(g) {
            var l =
                this._duration;
            g = this._soundById(g);
            return g && (l = this._sprite[g._sprite][1] / 1E3), l
        },
        state: function() {
            return this._state
        },
        unload: function() {
            for (var g = this._sounds, l = 0; l < g.length; l++) g[l]._paused || this.stop(g[l]._id), this._webAudio || (this._clearSound(g[l]._node), g[l]._node.removeEventListener("error", g[l]._errorFn, !1), g[l]._node.removeEventListener(t._canPlayEvent, g[l]._loadFn, !1), g[l]._node.removeEventListener("ended", g[l]._endFn, !1), t._releaseHtml5Audio(g[l]._node)), delete g[l]._node, this._clearTimer(g[l]._id);
            l = t._howls.indexOf(this);
            0 <= l && t._howls.splice(l, 1);
            g = !0;
            for (l = 0; l < t._howls.length; l++)
                if (t._howls[l]._src === this._src || 0 <= this._src.indexOf(t._howls[l]._src)) {
                    g = !1;
                    break
                } return L && g && delete L[this._src], t.noAudio = !1, this._state = "unloaded", this._sounds = [], null
        },
        on: function(g, l, v, t) {
            g = this["_on" + g];
            return "function" == typeof l && g.push(t ? {
                id: v,
                fn: l,
                once: t
            } : {
                id: v,
                fn: l
            }), this
        },
        off: function(g, l, v) {
            var r = this["_on" + g];
            if ("number" == typeof l && (v = l, l = null), l || v)
                for (g = 0; g < r.length; g++) {
                    var t = v === r[g].id;
                    if (l ===
                        r[g].fn && t || !l && t) {
                        r.splice(g, 1);
                        break
                    }
                } else if (g) this["_on" + g] = [];
                else
                    for (l = Object.keys(this), g = 0; g < l.length; g++) 0 === l[g].indexOf("_on") && Array.isArray(this[l[g]]) && (this[l[g]] = []);
            return this
        },
        once: function(g, l, v) {
            return this.on(g, l, v, 1), this
        },
        _emit: function(g, l, v) {
            for (var r = this["_on" + g], t = r.length - 1; 0 <= t; t--) r[t].id && r[t].id !== l && "load" !== g || (setTimeout(function(g) {
                g.call(this, l, v)
            }.bind(this, r[t].fn), 0), r[t].once && this.off(g, r[t].fn, r[t].id));
            return this._loadQueue(g), this
        },
        _loadQueue: function(g) {
            if (0 <
                this._queue.length) {
                var l = this._queue[0];
                l.event === g && (this._queue.shift(), this._loadQueue());
                g || l.action()
            }
            return this
        },
        _ended: function(g) {
            var l = g._sprite;
            if (!this._webAudio && g._node && !g._node.paused && !g._node.ended && g._node.currentTime < g._stop) return setTimeout(this._ended.bind(this, g), 100), this;
            l = !(!g._loop && !this._sprite[l][2]);
            if (this._emit("end", g._id), !this._webAudio && l && this.stop(g._id, !0).play(g._id), this._webAudio && l) {
                this._emit("play", g._id);
                g._seek = g._start || 0;
                g._rateSeek = 0;
                g._playStart =
                    t.ctx.currentTime;
                var v = 1E3 * (g._stop - g._start) / Math.abs(g._rate);
                this._endTimers[g._id] = setTimeout(this._ended.bind(this, g), v)
            }
            return this._webAudio && !l && (g._paused = !0, g._ended = !0, g._seek = g._start || 0, g._rateSeek = 0, this._clearTimer(g._id), this._cleanBuffer(g._node), t._autoSuspend()), this._webAudio || l || this.stop(g._id, !0), this
        },
        _clearTimer: function(g) {
            if (this._endTimers[g]) {
                if ("function" != typeof this._endTimers[g]) clearTimeout(this._endTimers[g]);
                else {
                    var l = this._soundById(g);
                    l && l._node && l._node.removeEventListener("ended",
                        this._endTimers[g], !1)
                }
                delete this._endTimers[g]
            }
            return this
        },
        _soundById: function(g) {
            for (var l = 0; l < this._sounds.length; l++)
                if (g === this._sounds[l]._id) return this._sounds[l];
            return null
        },
        _inactiveSound: function() {
            this._drain();
            for (var g = 0; g < this._sounds.length; g++)
                if (this._sounds[g]._ended) return this._sounds[g].reset();
            return new B(this)
        },
        _drain: function() {
            var g = this._pool,
                l = 0,
                v;
            if (!(this._sounds.length < g)) {
                for (v = 0; v < this._sounds.length; v++) this._sounds[v]._ended && l++;
                for (v = this._sounds.length - 1; 0 <=
                    v && !(l <= g); v--) this._sounds[v]._ended && (this._webAudio && this._sounds[v]._node && this._sounds[v]._node.disconnect(0), this._sounds.splice(v, 1), l--)
            }
        },
        _getSoundIds: function(g) {
            if (void 0 === g) {
                g = [];
                for (var l = 0; l < this._sounds.length; l++) g.push(this._sounds[l]._id);
                return g
            }
            return [g]
        },
        _refreshBuffer: function(g) {
            return g._node.bufferSource = t.ctx.createBufferSource(), g._node.bufferSource.buffer = L[this._src], g._panner ? g._node.bufferSource.connect(g._panner) : g._node.bufferSource.connect(g._node), g._node.bufferSource.loop =
                g._loop, g._loop && (g._node.bufferSource.loopStart = g._start || 0, g._node.bufferSource.loopEnd = g._stop || 0), g._node.bufferSource.playbackRate.setValueAtTime(g._rate, t.ctx.currentTime), this
        },
        _cleanBuffer: function(g) {
            var l = t._navigator && 0 <= t._navigator.vendor.indexOf("Apple");
            if (!g.bufferSource) return this;
            if (t._scratchBuffer && g.bufferSource && (g.bufferSource.onended = null, g.bufferSource.disconnect(0), l)) try {
                g.bufferSource.buffer = t._scratchBuffer
            } catch (y) {}
            return g.bufferSource = null, this
        },
        _clearSound: function(g) {
            /MSIE |Trident\//.test(t._navigator &&
                t._navigator.userAgent) || (g.src = "data:audio/wav;base64,UklGRigAAABXQVZFZm10IBIAAAABAAEARKwAAIhYAQACABAAAABkYXRhAgAAAAEA")
        }
    };
    var B = function(g) {
        this._parent = g;
        this.init()
    };
    B.prototype = {
        init: function() {
            var g = this._parent;
            return this._muted = g._muted, this._loop = g._loop, this._volume = g._volume, this._rate = g._rate, this._seek = 0, this._paused = !0, this._ended = !0, this._sprite = "__default", this._id = ++t._counter, g._sounds.push(this), this.create(), this
        },
        create: function() {
            var g = this._parent,
                l = t._muted || this._muted ||
                this._parent._muted ? 0 : this._volume;
            return g._webAudio ? (this._node = void 0 === t.ctx.createGain ? t.ctx.createGainNode() : t.ctx.createGain(), this._node.gain.setValueAtTime(l, t.ctx.currentTime), this._node.paused = !0, this._node.connect(t.masterGain)) : t.noAudio || (this._node = t._obtainHtml5Audio(), this._errorFn = this._errorListener.bind(this), this._node.addEventListener("error", this._errorFn, !1), this._loadFn = this._loadListener.bind(this), this._node.addEventListener(t._canPlayEvent, this._loadFn, !1), this._endFn =
                this._endListener.bind(this), this._node.addEventListener("ended", this._endFn, !1), this._node.src = g._src, this._node.preload = !0 === g._preload ? "auto" : g._preload, this._node.volume = l * t.volume(), this._node.load()), this
        },
        reset: function() {
            var g = this._parent;
            return this._muted = g._muted, this._loop = g._loop, this._volume = g._volume, this._rate = g._rate, this._seek = 0, this._rateSeek = 0, this._paused = !0, this._ended = !0, this._sprite = "__default", this._id = ++t._counter, this
        },
        _errorListener: function() {
            this._parent._emit("loaderror",
                this._id, this._node.error ? this._node.error.code : 0);
            this._node.removeEventListener("error", this._errorFn, !1)
        },
        _loadListener: function() {
            var g = this._parent;
            g._duration = Math.ceil(10 * this._node.duration) / 10;
            0 === Object.keys(g._sprite).length && (g._sprite = {
                __default: [0, 1E3 * g._duration]
            });
            "loaded" !== g._state && (g._state = "loaded", g._emit("load"), g._loadQueue());
            this._node.removeEventListener(t._canPlayEvent, this._loadFn, !1)
        },
        _endListener: function() {
            var g = this._parent;
            g._duration === 1 / 0 && (g._duration = Math.ceil(10 *
                this._node.duration) / 10, g._sprite.__default[1] === 1 / 0 && (g._sprite.__default[1] = 1E3 * g._duration), g._ended(this));
            this._node.removeEventListener("ended", this._endFn, !1)
        }
    };
    var L = {},
        M = function(g) {
            var l = g._src;
            if (L[l]) return g._duration = L[l].duration, void ea(g);
            if (/^data:[^;]+;base64,/.test(l)) {
                for (var t = atob(l.split(",")[1]), A = new Uint8Array(t.length), E = 0; E < t.length; ++E) A[E] = t.charCodeAt(E);
                v(A.buffer, g)
            } else {
                var B = new XMLHttpRequest;
                B.open(g._xhr.method, l, !0);
                B.withCredentials = g._xhr.withCredentials;
                B.responseType = "arraybuffer";
                g._xhr.headers && Object.keys(g._xhr.headers).forEach(function(l) {
                    B.setRequestHeader(l, g._xhr.headers[l])
                });
                B.onload = function() {
                    var l = (B.status + "")[0];
                    if ("0" !== l && "2" !== l && "3" !== l) return void g._emit("loaderror", null, "Failed loading audio file with status: " + B.status + ".");
                    v(B.response, g)
                };
                B.onerror = function() {
                    g._webAudio && (g._html5 = !0, g._webAudio = !1, g._sounds = [], delete L[l], g.load())
                };
                aa(B)
            }
        },
        aa = function(g) {
            try {
                g.send()
            } catch (r) {
                g.onerror()
            }
        },
        v = function(g, l) {
            var r = function() {
                    l._emit("loaderror",
                        null, "Decoding audio data failed.")
                },
                v = function(g) {
                    g && 0 < l._sounds.length ? (L[l._src] = g, ea(l, g)) : r()
                };
            "undefined" != typeof Promise && 1 === t.ctx.decodeAudioData.length ? t.ctx.decodeAudioData(g).then(v).catch(r) : t.ctx.decodeAudioData(g, v, r)
        },
        ea = function(g, l) {
            l && !g._duration && (g._duration = l.duration);
            0 === Object.keys(g._sprite).length && (g._sprite = {
                __default: [0, 1E3 * g._duration]
            });
            "loaded" !== g._state && (g._state = "loaded", g._emit("load"), g._loadQueue())
        },
        l = function() {
            if (t.usingWebAudio) {
                try {
                    "undefined" != typeof AudioContext ?
                        t.ctx = new AudioContext : "undefined" != typeof webkitAudioContext ? t.ctx = new webkitAudioContext : t.usingWebAudio = !1
                } catch (y) {
                    t.usingWebAudio = !1
                }
                t.ctx || (t.usingWebAudio = !1);
                var g = /iP(hone|od|ad)/.test(t._navigator && t._navigator.platform),
                    l = t._navigator && t._navigator.appVersion.match(/OS (\d+)_(\d+)_?(\d+)?/);
                l = l ? parseInt(l[1], 10) : null;
                g && l && 9 > l && (g = /safari/.test(t._navigator && t._navigator.userAgent.toLowerCase()), t._navigator && !g && (t.usingWebAudio = !1));
                t.usingWebAudio && (t.masterGain = void 0 === t.ctx.createGain ?
                    t.ctx.createGainNode() : t.ctx.createGain(), t.masterGain.gain.setValueAtTime(t._muted ? 0 : t._volume, t.ctx.currentTime), t.masterGain.connect(t.ctx.destination));
                t._setup()
            }
        };
    "function" == typeof define && define.amd && define([], function() {
        return {
            Howler: t,
            Howl: E
        }
    });
    "undefined" != typeof exports && (exports.Howler = t, exports.Howl = E);
    "undefined" != typeof global ? (global.HowlerGlobal = A, global.Howler = t, global.Howl = E, global.Sound = B) : "undefined" != typeof window && (window.HowlerGlobal = A, window.Howler = t, window.Howl = E, window.Sound =
        B)
}(), ! function() {
    HowlerGlobal.prototype._pos = [0, 0, 0];
    HowlerGlobal.prototype._orientation = [0, 0, -1, 0, 1, 0];
    HowlerGlobal.prototype.stereo = function(t) {
        if (!this.ctx || !this.ctx.listener) return this;
        for (var A = this._howls.length - 1; 0 <= A; A--) this._howls[A].stereo(t);
        return this
    };
    HowlerGlobal.prototype.pos = function(t, A, B) {
        return this.ctx && this.ctx.listener ? (A = "number" != typeof A ? this._pos[1] : A, B = "number" != typeof B ? this._pos[2] : B, "number" != typeof t ? this._pos : (this._pos = [t, A, B], void 0 !== this.ctx.listener.positionX ?
            (this.ctx.listener.positionX.setTargetAtTime(this._pos[0], Howler.ctx.currentTime, .1), this.ctx.listener.positionY.setTargetAtTime(this._pos[1], Howler.ctx.currentTime, .1), this.ctx.listener.positionZ.setTargetAtTime(this._pos[2], Howler.ctx.currentTime, .1)) : this.ctx.listener.setPosition(this._pos[0], this._pos[1], this._pos[2]), this)) : this
    };
    HowlerGlobal.prototype.orientation = function(t, A, B, L, M, aa) {
        if (!this.ctx || !this.ctx.listener) return this;
        var v = this._orientation;
        return A = "number" != typeof A ? v[1] : A, B =
            "number" != typeof B ? v[2] : B, L = "number" != typeof L ? v[3] : L, M = "number" != typeof M ? v[4] : M, aa = "number" != typeof aa ? v[5] : aa, "number" != typeof t ? v : (this._orientation = [t, A, B, L, M, aa], void 0 !== this.ctx.listener.forwardX ? (this.ctx.listener.forwardX.setTargetAtTime(t, Howler.ctx.currentTime, .1), this.ctx.listener.forwardY.setTargetAtTime(A, Howler.ctx.currentTime, .1), this.ctx.listener.forwardZ.setTargetAtTime(B, Howler.ctx.currentTime, .1), this.ctx.listener.upX.setTargetAtTime(L, Howler.ctx.currentTime, .1), this.ctx.listener.upY.setTargetAtTime(M,
                Howler.ctx.currentTime, .1), this.ctx.listener.upZ.setTargetAtTime(aa, Howler.ctx.currentTime, .1)) : this.ctx.listener.setOrientation(t, A, B, L, M, aa), this)
    };
    Howl.prototype.init = function(t) {
        return function(A) {
            return this._orientation = A.orientation || [1, 0, 0], this._stereo = A.stereo || null, this._pos = A.pos || null, this._pannerAttr = {
                coneInnerAngle: void 0 !== A.coneInnerAngle ? A.coneInnerAngle : 360,
                coneOuterAngle: void 0 !== A.coneOuterAngle ? A.coneOuterAngle : 360,
                coneOuterGain: void 0 !== A.coneOuterGain ? A.coneOuterGain : 0,
                distanceModel: void 0 !==
                    A.distanceModel ? A.distanceModel : "inverse",
                maxDistance: void 0 !== A.maxDistance ? A.maxDistance : 1E4,
                panningModel: void 0 !== A.panningModel ? A.panningModel : "HRTF",
                refDistance: void 0 !== A.refDistance ? A.refDistance : 1,
                rolloffFactor: void 0 !== A.rolloffFactor ? A.rolloffFactor : 1
            }, this._onstereo = A.onstereo ? [{
                fn: A.onstereo
            }] : [], this._onpos = A.onpos ? [{
                fn: A.onpos
            }] : [], this._onorientation = A.onorientation ? [{
                fn: A.onorientation
            }] : [], t.call(this, A)
        }
    }(Howl.prototype.init);
    Howl.prototype.stereo = function(t, E) {
        var B = this;
        if (!B._webAudio) return B;
        if ("loaded" !== B._state) return B._queue.push({
            event: "stereo",
            action: function() {
                B.stereo(t, E)
            }
        }), B;
        var L = void 0 === Howler.ctx.createStereoPanner ? "spatial" : "stereo";
        if (void 0 === E) {
            if ("number" != typeof t) return B._stereo;
            B._stereo = t;
            B._pos = [t, 0, 0]
        }
        for (var M = B._getSoundIds(E), aa = 0; aa < M.length; aa++) {
            var v = B._soundById(M[aa]);
            if (v) {
                if ("number" != typeof t) return v._stereo;
                v._stereo = t;
                v._pos = [t, 0, 0];
                v._node && (v._pannerAttr.panningModel = "equalpower", v._panner && v._panner.pan || A(v, L), "spatial" === L ? void 0 !== v._panner.positionX ?
                    (v._panner.positionX.setValueAtTime(t, Howler.ctx.currentTime), v._panner.positionY.setValueAtTime(0, Howler.ctx.currentTime), v._panner.positionZ.setValueAtTime(0, Howler.ctx.currentTime)) : v._panner.setPosition(t, 0, 0) : v._panner.pan.setValueAtTime(t, Howler.ctx.currentTime));
                B._emit("stereo", v._id)
            }
        }
        return B
    };
    Howl.prototype.pos = function(t, E, B, L) {
        var M = this;
        if (!M._webAudio) return M;
        if ("loaded" !== M._state) return M._queue.push({
            event: "pos",
            action: function() {
                M.pos(t, E, B, L)
            }
        }), M;
        if (E = "number" != typeof E ? 0 : E,
            B = "number" != typeof B ? -.5 : B, void 0 === L) {
            if ("number" != typeof t) return M._pos;
            M._pos = [t, E, B]
        }
        for (var aa = M._getSoundIds(L), v = 0; v < aa.length; v++) {
            var ea = M._soundById(aa[v]);
            if (ea) {
                if ("number" != typeof t) return ea._pos;
                ea._pos = [t, E, B];
                ea._node && (ea._panner && !ea._panner.pan || A(ea, "spatial"), void 0 !== ea._panner.positionX ? (ea._panner.positionX.setValueAtTime(t, Howler.ctx.currentTime), ea._panner.positionY.setValueAtTime(E, Howler.ctx.currentTime), ea._panner.positionZ.setValueAtTime(B, Howler.ctx.currentTime)) :
                    ea._panner.setPosition(t, E, B));
                M._emit("pos", ea._id)
            }
        }
        return M
    };
    Howl.prototype.orientation = function(t, E, B, L) {
        var M = this;
        if (!M._webAudio) return M;
        if ("loaded" !== M._state) return M._queue.push({
            event: "orientation",
            action: function() {
                M.orientation(t, E, B, L)
            }
        }), M;
        if (E = "number" != typeof E ? M._orientation[1] : E, B = "number" != typeof B ? M._orientation[2] : B, void 0 === L) {
            if ("number" != typeof t) return M._orientation;
            M._orientation = [t, E, B]
        }
        for (var aa = M._getSoundIds(L), v = 0; v < aa.length; v++) {
            var ea = M._soundById(aa[v]);
            if (ea) {
                if ("number" !=
                    typeof t) return ea._orientation;
                ea._orientation = [t, E, B];
                ea._node && (ea._panner || (ea._pos || (ea._pos = M._pos || [0, 0, -.5]), A(ea, "spatial")), void 0 !== ea._panner.orientationX ? (ea._panner.orientationX.setValueAtTime(t, Howler.ctx.currentTime), ea._panner.orientationY.setValueAtTime(E, Howler.ctx.currentTime), ea._panner.orientationZ.setValueAtTime(B, Howler.ctx.currentTime)) : ea._panner.setOrientation(t, E, B));
                M._emit("orientation", ea._id)
            }
        }
        return M
    };
    Howl.prototype.pannerAttr = function() {
        var t, E, B = arguments;
        if (!this._webAudio) return this;
        if (0 === B.length) return this._pannerAttr;
        if (1 === B.length) {
            if ("object" != typeof B[0]) return E = this._soundById(parseInt(B[0], 10)), E ? E._pannerAttr : this._pannerAttr;
            var L = B[0];
            void 0 === t && (L.pannerAttr || (L.pannerAttr = {
                coneInnerAngle: L.coneInnerAngle,
                coneOuterAngle: L.coneOuterAngle,
                coneOuterGain: L.coneOuterGain,
                distanceModel: L.distanceModel,
                maxDistance: L.maxDistance,
                refDistance: L.refDistance,
                rolloffFactor: L.rolloffFactor,
                panningModel: L.panningModel
            }), this._pannerAttr = {
                coneInnerAngle: void 0 !== L.pannerAttr.coneInnerAngle ?
                    L.pannerAttr.coneInnerAngle : this._coneInnerAngle,
                coneOuterAngle: void 0 !== L.pannerAttr.coneOuterAngle ? L.pannerAttr.coneOuterAngle : this._coneOuterAngle,
                coneOuterGain: void 0 !== L.pannerAttr.coneOuterGain ? L.pannerAttr.coneOuterGain : this._coneOuterGain,
                distanceModel: void 0 !== L.pannerAttr.distanceModel ? L.pannerAttr.distanceModel : this._distanceModel,
                maxDistance: void 0 !== L.pannerAttr.maxDistance ? L.pannerAttr.maxDistance : this._maxDistance,
                refDistance: void 0 !== L.pannerAttr.refDistance ? L.pannerAttr.refDistance : this._refDistance,
                rolloffFactor: void 0 !== L.pannerAttr.rolloffFactor ? L.pannerAttr.rolloffFactor : this._rolloffFactor,
                panningModel: void 0 !== L.pannerAttr.panningModel ? L.pannerAttr.panningModel : this._panningModel
            })
        } else 2 === B.length && (L = B[0], t = parseInt(B[1], 10));
        t = this._getSoundIds(t);
        for (B = 0; B < t.length; B++)
            if (E = this._soundById(t[B])) {
                var M = E._pannerAttr;
                M = {
                    coneInnerAngle: void 0 !== L.coneInnerAngle ? L.coneInnerAngle : M.coneInnerAngle,
                    coneOuterAngle: void 0 !== L.coneOuterAngle ? L.coneOuterAngle : M.coneOuterAngle,
                    coneOuterGain: void 0 !== L.coneOuterGain ? L.coneOuterGain : M.coneOuterGain,
                    distanceModel: void 0 !== L.distanceModel ? L.distanceModel : M.distanceModel,
                    maxDistance: void 0 !== L.maxDistance ? L.maxDistance : M.maxDistance,
                    refDistance: void 0 !== L.refDistance ? L.refDistance : M.refDistance,
                    rolloffFactor: void 0 !== L.rolloffFactor ? L.rolloffFactor : M.rolloffFactor,
                    panningModel: void 0 !== L.panningModel ? L.panningModel : M.panningModel
                };
                var aa = E._panner;
                aa || (E._pos || (E._pos = this._pos || [0, 0, -.5]), A(E, "spatial"), aa = E._panner);
                aa.coneInnerAngle =
                    M.coneInnerAngle;
                aa.coneOuterAngle = M.coneOuterAngle;
                aa.coneOuterGain = M.coneOuterGain;
                aa.distanceModel = M.distanceModel;
                aa.maxDistance = M.maxDistance;
                aa.refDistance = M.refDistance;
                aa.rolloffFactor = M.rolloffFactor;
                aa.panningModel = M.panningModel
            } return this
    };
    Sound.prototype.init = function(t) {
        return function() {
            var A = this._parent;
            this._orientation = A._orientation;
            this._stereo = A._stereo;
            this._pos = A._pos;
            this._pannerAttr = A._pannerAttr;
            t.call(this);
            this._stereo ? A.stereo(this._stereo) : this._pos && A.pos(this._pos[0],
                this._pos[1], this._pos[2], this._id)
        }
    }(Sound.prototype.init);
    Sound.prototype.reset = function(t) {
        return function() {
            var A = this._parent;
            return this._orientation = A._orientation, this._stereo = A._stereo, this._pos = A._pos, this._pannerAttr = A._pannerAttr, this._stereo ? A.stereo(this._stereo) : this._pos ? A.pos(this._pos[0], this._pos[1], this._pos[2], this._id) : this._panner && (this._panner.disconnect(0), this._panner = void 0, A._refreshBuffer(this)), t.call(this)
        }
    }(Sound.prototype.reset);
    var A = function(t, A) {
        "spatial" === (A ||
            "spatial") ? (t._panner = Howler.ctx.createPanner(), t._panner.coneInnerAngle = t._pannerAttr.coneInnerAngle, t._panner.coneOuterAngle = t._pannerAttr.coneOuterAngle, t._panner.coneOuterGain = t._pannerAttr.coneOuterGain, t._panner.distanceModel = t._pannerAttr.distanceModel, t._panner.maxDistance = t._pannerAttr.maxDistance, t._panner.refDistance = t._pannerAttr.refDistance, t._panner.rolloffFactor = t._pannerAttr.rolloffFactor, t._panner.panningModel = t._pannerAttr.panningModel, void 0 !== t._panner.positionX ? (t._panner.positionX.setValueAtTime(t._pos[0],
            Howler.ctx.currentTime), t._panner.positionY.setValueAtTime(t._pos[1], Howler.ctx.currentTime), t._panner.positionZ.setValueAtTime(t._pos[2], Howler.ctx.currentTime)) : t._panner.setPosition(t._pos[0], t._pos[1], t._pos[2]), void 0 !== t._panner.orientationX ? (t._panner.orientationX.setValueAtTime(t._orientation[0], Howler.ctx.currentTime), t._panner.orientationY.setValueAtTime(t._orientation[1], Howler.ctx.currentTime), t._panner.orientationZ.setValueAtTime(t._orientation[2], Howler.ctx.currentTime)) : t._panner.setOrientation(t._orientation[0],
            t._orientation[1], t._orientation[2])) : (t._panner = Howler.ctx.createStereoPanner(), t._panner.pan.setValueAtTime(t._stereo, Howler.ctx.currentTime));
        t._panner.connect(t._node);
        t._paused || t._parent.pause(t._id, !0).play(t._id, !0)
    }
}());
