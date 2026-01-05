/*
 * lime-init/01-lime-openfl-base.js
 * Part 1/8: Lime Framework Core + OpenFL Base
 * Contains: lime.app.*, lime._internal.*, openfl.events.*, openfl.display.* (base classes)
 */
var $lime_init = function(A, t) {
var E = function(t, E) {
    (function(t, E) {
            function v(a, b) {
                a = Object.create(a);
                for (var c in b) a[c] = b[c];
                b.toString !== Object.prototype.toString && (a.toString = b.toString);
                return a
            }

            function L(a) {
                return a instanceof Array ? new yf(a) : a.iterator()
            }

            function l(a, b) {
                if (null == b) return null;
                null == b.__id__ && (b.__id__ = E.$haxeUID++);
                var c;
                null == a.hx__closures__ ? a.hx__closures__ = {} : c = a.hx__closures__[b.__id__];
                null == c && (c = b.bind(a), a.hx__closures__[b.__id__] = c);
                return c
            }
            t.lime = t.lime || {};
            var g = {},
                r = function() {
                    return va.__string_rec(this, "")
                },
                y = y || {},
                G, Za = function() {};
            g["lime.app.IModule"] = Za;
            Za.__name__ = "lime.app.IModule";
            Za.__isInterface__ = !0;
            Za.prototype = {
                __class__: Za
            };
            var sa = function() {
                this.onExit = new zf
            };
            g["lime.app.Module"] = sa;
            sa.__name__ = "lime.app.Module";
            sa.__interfaces__ = [Za];
            sa.prototype = {
                __registerLimeModule: function(a) {},
                __unregisterLimeModule: function(a) {},
                __class__: sa
            };
            var B = function(a) {
                this.gameDeviceCache = new mc;
                this.parent = a;
                this.nextUpdate = this.lastUpdate = this.currentUpdate =
                    0;
                this.framePeriod = -1;
                Ge.init();
                this.accelerometer = He.registerSensor(tl.ACCELEROMETER, 0)
            };
            g["lime._internal.backend.html5.HTML5Application"] = B;
            B.__name__ = "lime._internal.backend.html5.HTML5Application";
            B.prototype = {
                convertKeyCode: function(a) {
                    if (65 <= a && 90 >= a) return a + 32;
                    switch (a) {
                        case 12:
                            return 1073741980;
                        case 16:
                            return 1073742049;
                        case 17:
                            return 1073742048;
                        case 18:
                            return 1073742050;
                        case 19:
                            return 1073741896;
                        case 20:
                            return 1073741881;
                        case 33:
                            return 1073741899;
                        case 34:
                            return 1073741902;
                        case 35:
                            return 1073741901;
                        case 36:
                            return 1073741898;
                        case 37:
                            return 1073741904;
                        case 38:
                            return 1073741906;
                        case 39:
                            return 1073741903;
                        case 40:
                            return 1073741905;
                        case 41:
                            return 1073741943;
                        case 43:
                            return 1073741940;
                        case 44:
                            return 1073741894;
                        case 45:
                            return 1073741897;
                        case 46:
                            return 127;
                        case 91:
                            return 1073742051;
                        case 92:
                            return 1073742055;
                        case 93:
                            return 1073742055;
                        case 95:
                            return 1073742106;
                        case 96:
                            return 1073741922;
                        case 97:
                            return 1073741913;
                        case 98:
                            return 1073741914;
                        case 99:
                            return 1073741915;
                        case 100:
                            return 1073741916;
                        case 101:
                            return 1073741917;
                        case 102:
                            return 1073741918;
                        case 103:
                            return 1073741919;
                        case 104:
                            return 1073741920;
                        case 105:
                            return 1073741921;
                        case 106:
                            return 1073741909;
                        case 107:
                            return 1073741911;
                        case 108:
                            return 1073741923;
                        case 109:
                            return 1073741910;
                        case 110:
                            return 1073741923;
                        case 111:
                            return 1073741908;
                        case 112:
                            return 1073741882;
                        case 113:
                            return 1073741883;
                        case 114:
                            return 1073741884;
                        case 115:
                            return 1073741885;
                        case 116:
                            return 1073741886;
                        case 117:
                            return 1073741887;
                        case 118:
                            return 1073741888;
                        case 119:
                            return 1073741889;
                        case 120:
                            return 1073741890;
                        case 121:
                            return 1073741891;
                        case 122:
                            return 1073741892;
                        case 123:
                            return 1073741893;
                        case 124:
                            return 1073741928;
                        case 125:
                            return 1073741929;
                        case 126:
                            return 1073741930;
                        case 127:
                            return 1073741931;
                        case 128:
                            return 1073741932;
                        case 129:
                            return 1073741933;
                        case 130:
                            return 1073741934;
                        case 131:
                            return 1073741935;
                        case 132:
                            return 1073741936;
                        case 133:
                            return 1073741937;
                        case 134:
                            return 1073741938;
                        case 135:
                            return 1073741939;
                        case 144:
                            return 1073741907;
                        case 145:
                            return 1073741895;
                        case 160:
                            return 94;
                        case 161:
                            return 33;
                        case 163:
                            return 35;
                        case 164:
                            return 36;
                        case 166:
                            return 1073742094;
                        case 167:
                            return 1073742095;
                        case 168:
                            return 1073742097;
                        case 169:
                            return 41;
                        case 170:
                            return 42;
                        case 171:
                            return 96;
                        case 172:
                            return 1073741898;
                        case 173:
                            return 45;
                        case 174:
                            return 1073741953;
                        case 175:
                            return 1073741952;
                        case 176:
                            return 1073742082;
                        case 177:
                            return 1073742083;
                        case 178:
                            return 1073742084;
                        case 179:
                            return 1073742085;
                        case 180:
                            return 1073742089;
                        case 181:
                            return 1073742086;
                        case 182:
                            return 1073741953;
                        case 183:
                            return 1073741952;
                        case 186:
                            return 59;
                        case 187:
                            return 61;
                        case 188:
                            return 44;
                        case 189:
                            return 45;
                        case 190:
                            return 46;
                        case 191:
                            return 47;
                        case 192:
                            return 96;
                        case 193:
                            return 63;
                        case 194:
                            return 1073741923;
                        case 219:
                            return 91;
                        case 220:
                            return 92;
                        case 221:
                            return 93;
                        case 222:
                            return 39;
                        case 223:
                            return 96;
                        case 224:
                            return 1073742051;
                        case 226:
                            return 92
                    }
                    return a
                },
                exec: function() {
                    window.addEventListener("keydown", l(this, this.handleKeyEvent), !1);
                    window.addEventListener("keyup", l(this, this.handleKeyEvent), !1);
                    window.addEventListener("focus", l(this, this.handleWindowEvent),
                        !1);
                    window.addEventListener("blur", l(this, this.handleWindowEvent), !1);
                    window.addEventListener("resize", l(this, this.handleWindowEvent), !1);
                    window.addEventListener("beforeunload", l(this, this.handleWindowEvent), !1);
                    Object.prototype.hasOwnProperty.call(window, "Accelerometer") && window.addEventListener("devicemotion", l(this, this.handleSensorEvent), !1);
                    CanvasRenderingContext2D.prototype.isPointInStroke || (CanvasRenderingContext2D.prototype.isPointInStroke = function(a, b, c) {
                        return !1
                    });
                    CanvasRenderingContext2D.prototype.isPointInPath ||
                        (CanvasRenderingContext2D.prototype.isPointInPath = function(a, b, c) {
                            return !1
                        });
                    0 == "performance" in window && (window.performance = {});
                    if (0 == "now" in window.performance) {
                        var a = Date.now();
                        performance.timing && performance.timing.navigationStart && (a = performance.timing.navigationStart);
                        window.performance.now = function() {
                            return Date.now() - a
                        }
                    }
                    for (var b = 0, c = ["ms", "moz", "webkit", "o"], d = 0; d < c.length && !window.requestAnimationFrame; ++d) window.requestAnimationFrame = window[c[d] + "RequestAnimationFrame"], window.cancelAnimationFrame =
                        window[c[d] + "CancelAnimationFrame"] || window[c[d] + "CancelRequestAnimationFrame"];
                    window.requestAnimationFrame || (window.requestAnimationFrame = function(a, c) {
                        var d = window.performance.now(),
                            f = Math.max(0, 16 - (d - b));
                        c = window.setTimeout(function() {
                            a(d + f)
                        }, f);
                        b = d + f;
                        return c
                    });
                    window.cancelAnimationFrame || (window.cancelAnimationFrame = function(a) {
                        clearTimeout(a)
                    });
                    window.requestAnimFrame = window.requestAnimationFrame;
                    this.lastUpdate = window.performance.now();
                    this.handleApplicationEvent();
                    return 0
                },
                exit: function() {},
                handleApplicationEvent: function(a) {
                    a = 0;
                    for (var b = this.parent.__windows; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.__backend.updateSize()
                    }
                    this.updateGameDevices();
                    this.currentUpdate = window.performance.now();
                    if (this.currentUpdate >= this.nextUpdate) {
                        this.deltaTime = this.currentUpdate - this.lastUpdate;
                        a = 0;
                        for (b = this.parent.__windows; a < b.length;) c = b[a], ++a, this.parent.onUpdate.dispatch(this.deltaTime | 0), null != c.context && c.onRender.dispatch(c.context);
                        this.nextUpdate = 0 > this.framePeriod ? this.currentUpdate : this.currentUpdate -
                            this.currentUpdate % this.framePeriod + this.framePeriod;
                        this.lastUpdate = this.currentUpdate
                    }
                    window.requestAnimationFrame(l(this, this.handleApplicationEvent))
                },
                handleKeyEvent: function(a) {
                    if (null != this.parent.__window) {
                        var b = this.convertKeyCode(null != a.keyCode ? a.keyCode : a.which),
                            c = (a.shiftKey ? 3 : 0) | (a.ctrlKey ? 192 : 0) | (a.altKey ? 768 : 0) | (a.metaKey ? 3072 : 0);
                        "keydown" == a.type ? (this.parent.__window.onKeyDown.dispatch(b, c), this.parent.__window.onKeyDown.canceled && a.cancelable && a.preventDefault()) : (this.parent.__window.onKeyUp.dispatch(b,
                            c), this.parent.__window.onKeyUp.canceled && a.cancelable && a.preventDefault())
                    }
                },
                handleSensorEvent: function(a) {
                    this.accelerometer.onUpdate.dispatch(a.accelerationIncludingGravity.x, a.accelerationIncludingGravity.y, a.accelerationIncludingGravity.z)
                },
                handleWindowEvent: function(a) {
                    if (null != this.parent.__window) switch (a.type) {
                        case "blur":
                            this.hidden || (this.parent.__window.onFocusOut.dispatch(), this.parent.__window.onDeactivate.dispatch(), this.hidden = !0);
                            break;
                        case "focus":
                            this.hidden && (this.parent.__window.onFocusIn.dispatch(),
                                this.parent.__window.onActivate.dispatch(), this.hidden = !1);
                            break;
                        case "resize":
                            this.parent.__window.__backend.handleResizeEvent(a);
                            break;
                        case "visibilitychange":
                            window.document.hidden ? this.hidden || (this.parent.__window.onFocusOut.dispatch(), this.parent.__window.onDeactivate.dispatch(), this.hidden = !0) : this.hidden && (this.parent.__window.onFocusIn.dispatch(), this.parent.__window.onActivate.dispatch(), this.hidden = !1)
                    }
                },
                updateGameDevices: function() {
                    var a = nc.__getDeviceData();
                    if (null != a)
                        for (var b, c, d, f,
                                h, k = 0, n = a.length; k < n;)
                            if (b = k++, f = a[b], null != f) {
                                if (!this.gameDeviceCache.h.hasOwnProperty(b)) {
                                    h = new Rj;
                                    h.id = b;
                                    h.connected = f.connected;
                                    c = 0;
                                    for (d = f.buttons.length; c < d;) {
                                        var p = c++;
                                        h.buttons.push(f.buttons[p].value)
                                    }
                                    c = 0;
                                    for (d = f.axes.length; c < d;) p = c++, h.axes.push(f.axes[p]);
                                    "standard" == f.mapping && (h.isGamepad = !0);
                                    this.gameDeviceCache.h[b] = h;
                                    f.connected && (nc.__connect(b), h.isGamepad && qc.__connect(b))
                                }
                                h = this.gameDeviceCache.h[b];
                                d = nc.devices.h[b];
                                c = qc.devices.h[b];
                                if (f.connected) {
                                    for (var P = 0, g = f.buttons.length; P <
                                        g;) {
                                        var m = P++;
                                        p = f.buttons[m].value;
                                        if (p != h.buttons[m]) {
                                            if (6 == m) d.onAxisMove.dispatch(f.axes.length, p), null != c && c.onAxisMove.dispatch(4, p);
                                            else if (7 == m) d.onAxisMove.dispatch(f.axes.length + 1, p), null != c && c.onAxisMove.dispatch(5, p);
                                            else if (0 < p ? d.onButtonDown.dispatch(m) : d.onButtonUp.dispatch(m), null != c) {
                                                switch (m) {
                                                    case 0:
                                                        b = 0;
                                                        break;
                                                    case 1:
                                                        b = 1;
                                                        break;
                                                    case 2:
                                                        b = 2;
                                                        break;
                                                    case 3:
                                                        b = 3;
                                                        break;
                                                    case 4:
                                                        b = 9;
                                                        break;
                                                    case 5:
                                                        b = 10;
                                                        break;
                                                    case 8:
                                                        b = 4;
                                                        break;
                                                    case 9:
                                                        b = 6;
                                                        break;
                                                    case 10:
                                                        b = 7;
                                                        break;
                                                    case 11:
                                                        b = 8;
                                                        break;
                                                    case 12:
                                                        b = 11;
                                                        break;
                                                    case 13:
                                                        b = 12;
                                                        break;
                                                    case 14:
                                                        b = 13;
                                                        break;
                                                    case 15:
                                                        b = 14;
                                                        break;
                                                    case 16:
                                                        b = 5;
                                                        break;
                                                    default:
                                                        continue
                                                }
                                                0 < p ? c.onButtonDown.dispatch(b) : c.onButtonUp.dispatch(b)
                                            }
                                            h.buttons[m] = p
                                        }
                                    }
                                    b = 0;
                                    for (p = f.axes.length; b < p;) P = b++, f.axes[P] != h.axes[P] && (d.onAxisMove.dispatch(P, f.axes[P]), null != c && c.onAxisMove.dispatch(P, f.axes[P]), h.axes[P] = f.axes[P])
                                } else h.connected && (h.connected = !1, nc.__disconnect(b), qc.__disconnect(b))
                            }
                },
                __class__: B
            };
            var A = function() {
                this.onCreateWindow = new Sj;
                this.onUpdate = new zf;
                this.onExit = new zf;
                null == A.current &&
                    (A.current = this);
                this.meta = new Qa;
                this.modules = [];
                this.__windowByID = new mc;
                this.__windows = [];
                this.__backend = new B(this);
                this.__registerLimeModule(this);
                this.__preloader = new Tj;
                this.__preloader.onProgress.add(l(this, this.onPreloadProgress));
                this.__preloader.onComplete.add(l(this, this.onPreloadComplete))
            };
            g["lime.app.Application"] = A;
            A.__name__ = "lime.app.Application";
            A.__super__ = sa;
            A.prototype = v(sa.prototype, {
                addModule: function(a) {
                    a.__registerLimeModule(this);
                    this.modules.push(a)
                },
                exec: function() {
                    A.current =
                        this;
                    return this.__backend.exec()
                },
                onGamepadAxisMove: function(a, b, c) {},
                onGamepadButtonDown: function(a, b) {},
                onGamepadButtonUp: function(a, b) {},
                onGamepadConnect: function(a) {},
                onGamepadDisconnect: function(a) {},
                onJoystickAxisMove: function(a, b, c) {},
                onJoystickButtonDown: function(a, b) {},
                onJoystickButtonUp: function(a, b) {},
                onJoystickConnect: function(a) {},
                onJoystickDisconnect: function(a) {},
                onJoystickHatMove: function(a, b, c) {},
                onKeyDown: function(a, b) {},
                onKeyUp: function(a, b) {},
                onModuleExit: function(a) {},
                onMouseDown: function(a,
                    b, c) {},
                onMouseMove: function(a, b) {},
                onMouseMoveRelative: function(a, b) {},
                onMouseUp: function(a, b, c) {},
                onMouseWheel: function(a, b, c) {},
                onPreloadComplete: function() {},
                onPreloadProgress: function(a, b) {},
                onRenderContextLost: function() {},
                onRenderContextRestored: function(a) {},
                onTextEdit: function(a, b, c) {},
                onTextInput: function(a) {},
                onTouchCancel: function(a) {},
                onTouchEnd: function(a) {},
                onTouchMove: function(a) {},
                onTouchStart: function(a) {},
                onWindowActivate: function() {},
                onWindowClose: function() {},
                onWindowCreate: function() {},
                onWindowDeactivate: function() {},
                onWindowDropFile: function(a) {},
                onWindowEnter: function() {},
                onWindowExpose: function() {},
                onWindowFocusIn: function() {},
                onWindowFocusOut: function() {},
                onWindowFullscreen: function() {},
                onWindowLeave: function() {},
                onWindowMove: function(a, b) {},
                onWindowMinimize: function() {},
                onWindowResize: function(a, b) {},
                onWindowRestore: function() {},
                removeModule: function(a) {
                    null != a && (a.__unregisterLimeModule(this), N.remove(this.modules, a))
                },
                render: function(a) {},
                update: function(a) {},
                __registerLimeModule: function(a) {
                    a.onUpdate.add(l(this,
                        this.update));
                    a.onExit.add(l(this, this.onModuleExit), !1, 0);
                    a.onExit.add(l(this, this.__onModuleExit), !1, -1E3);
                    for (a = qc.devices.iterator(); a.hasNext();) {
                        var b = a.next();
                        this.__onGamepadConnect(b)
                    }
                    qc.onConnect.add(l(this, this.__onGamepadConnect));
                    for (a = nc.devices.iterator(); a.hasNext();) b = a.next(), this.__onJoystickConnect(b);
                    nc.onConnect.add(l(this, this.__onJoystickConnect));
                    dc.onCancel.add(l(this, this.onTouchCancel));
                    dc.onStart.add(l(this, this.onTouchStart));
                    dc.onMove.add(l(this, this.onTouchMove));
                    dc.onEnd.add(l(this,
                        this.onTouchEnd))
                },
                __removeWindow: function(a) {
                    null != a && this.__windowByID.h.hasOwnProperty(a.id) && (this.__window == a && (this.__window = null), N.remove(this.__windows, a), this.__windowByID.remove(a.id), a.close(), this.__checkForAllWindowsClosed())
                },
                __checkForAllWindowsClosed: function() {
                    0 == this.__windows.length && Cc.exit(0)
                },
                __onGamepadConnect: function(a) {
                    this.onGamepadConnect(a);
                    var b = this,
                        c = function(c, d) {
                            b.onGamepadAxisMove(a, c, d)
                        };
                    a.onAxisMove.add(c);
                    var d = this;
                    c = function(b) {
                        d.onGamepadButtonDown(a, b)
                    };
                    a.onButtonDown.add(c);
                    var f = this;
                    c = function(b) {
                        f.onGamepadButtonUp(a, b)
                    };
                    a.onButtonUp.add(c);
                    var h = this;
                    a.onDisconnect.add(function() {
                        h.onGamepadDisconnect(a)
                    })
                },
                __onJoystickConnect: function(a) {
                    this.onJoystickConnect(a);
                    var b = this,
                        c = function(c, d) {
                            b.onJoystickAxisMove(a, c, d)
                        };
                    a.onAxisMove.add(c);
                    var d = this;
                    c = function(b) {
                        d.onJoystickButtonDown(a, b)
                    };
                    a.onButtonDown.add(c);
                    var f = this;
                    c = function(b) {
                        f.onJoystickButtonUp(a, b)
                    };
                    a.onButtonUp.add(c);
                    var h = this;
                    a.onDisconnect.add(function() {
                        h.onJoystickDisconnect(a)
                    });
                    var k = this;
                    c = function(b, c) {
                        k.onJoystickHatMove(a, b, c)
                    };
                    a.onHatMove.add(c)
                },
                __onModuleExit: function(a) {
                    this.onExit.canceled || (this.__unregisterLimeModule(this), this.__backend.exit(), A.current == this && (A.current = null))
                },
                __onWindowClose: function(a) {
                    if (this.__window == a) this.onWindowClose();
                    this.__removeWindow(a)
                },
                __unregisterLimeModule: function(a) {
                    a.onUpdate.remove(l(this, this.update));
                    a.onExit.remove(l(this, this.__onModuleExit));
                    a.onExit.remove(l(this, this.onModuleExit));
                    qc.onConnect.remove(l(this, this.__onGamepadConnect));
                    nc.onConnect.remove(l(this, this.__onJoystickConnect));
                    dc.onCancel.remove(l(this, this.onTouchCancel));
                    dc.onStart.remove(l(this, this.onTouchStart));
                    dc.onMove.remove(l(this, this.onTouchMove));
                    dc.onEnd.remove(l(this, this.onTouchEnd))
                },
                __class__: A
            });
            var aa = function() {};
            g.ApplicationMain = aa;
            aa.__name__ = "ApplicationMain";
            aa.main = function() {
                Cc.__registerEntryPoint("mfcg", aa.create)
            };
            aa.create = function(a) {
                var b = new Wg;
                Ab.init(a);
                b.meta.h.build = "2430";
                b.meta.h.company = "Retronic Games";
                b.meta.h.file = "mfcg";
                b.meta.h.name = "Medieval Fantasy City Generator";
                b.meta.h.packageName = "com.watabou.mfcg";
                b.meta.h.version = "0.11.5";
                var c = {
                    allowHighDPI: !0,
                    alwaysOnTop: !1,
                    borderless: !1,
                    element: null,
                    frameRate: 60,
                    height: 0,
                    hidden: !1,
                    maximized: !1,
                    minimized: !1,
                    parameters: {},
                    resizable: !0,
                    title: "Medieval Fantasy City Generator",
                    width: 0,
                    x: null,
                    y: null,
                    context: {
                        antialiasing: 0,
                        background: 13419960,
                        colorDepth: 32,
                        depth: !0,
                        hardware: !0,
                        stencil: !0,
                        type: null,
                        vsync: !0
                    }
                };
                if (null == b.__window && null != a)
                    for (var d = 0, f = ya.fields(a); d < f.length;) {
                        var h =
                            f[d];
                        ++d;
                        Object.prototype.hasOwnProperty.call(c, h) ? c[h] = ya.field(a, h) : Object.prototype.hasOwnProperty.call(c.context, h) && (c.context[h] = ya.field(a, h))
                    }
                b.createWindow(c);
                var k = new Lh(new Xg);
                b.__preloader.onProgress.add(function(a, b) {
                    k.update(a, b)
                });
                b.__preloader.onComplete.add(function() {
                    k.start()
                });
                var n = b.__window.stage;
                k.onComplete.add(function() {
                    aa.start(n)
                });
                d = 0;
                for (f = Ab.preloadLibraries; d < f.length;) a = f[d], ++d, b.__preloader.addLibrary(a);
                d = 0;
                for (f = Ab.preloadLibraryNames; d < f.length;) a = f[d], ++d,
                    b.__preloader.addLibraryName(a);
                b.__preloader.load();
                b.exec()
            };
            aa.start = function(a) {
                if (a.__uncaughtErrorEvents.__enabled) try {
                    var b = a.getChildAt(0);
                    null != b && b instanceof kb || (b = new Af, a.addChild(b));
                    new rc(b);
                    a.dispatchEvent(new wa("resize", !1, !1));
                    a.window.__fullscreen && a.dispatchEvent(new df("fullScreen", !1, !1, !0, !0))
                } catch (c) {
                    Ta.lastError = c, b = X.caught(c).unwrap(), a.__handleError(b)
                } else b = a.getChildAt(0), null != b && b instanceof kb || (b = new Af, a.addChild(b)), new rc(b), a.dispatchEvent(new wa("resize",
                    !1, !1)), a.window.__fullscreen && a.dispatchEvent(new df("fullScreen", !1, !1, !0, !0))
            };
            var M = function() {};
            g["openfl.events.IEventDispatcher"] = M;
            M.__name__ = "openfl.events.IEventDispatcher";
            M.__isInterface__ = !0;
            var oa = function(a) {
                null != a && (this.__targetDispatcher = a)
            };
            g["openfl.events.EventDispatcher"] = oa;
            oa.__name__ = "openfl.events.EventDispatcher";
            oa.__interfaces__ = [M];
            oa.prototype = {
                addEventListener: function(a, b, c, d, f) {
                    null == f && (f = !1);
                    null == d && (d = 0);
                    null == c && (c = !1);
                    if (null != b)
                        if (null == this.__eventMap && (this.__eventMap =
                                new Qa, this.__iterators = new Qa), Object.prototype.hasOwnProperty.call(this.__eventMap.h, a)) {
                            p = this.__eventMap.h[a];
                            for (var h = 0, k = p.length; h < k;) {
                                var n = h++;
                                if (p[n].match(b, c)) return
                            }
                            a = this.__iterators.h[a];
                            for (h = 0; h < a.length;) k = a[h], ++h, k.active && k.copy();
                            this.__addListenerByPriority(p, new Bf(b, c, d, f))
                        } else {
                            var p = [];
                            p.push(new Bf(b, c, d, f));
                            k = new Yg(p);
                            this.__eventMap.h[a] = p;
                            this.__iterators.h[a] = [k]
                        }
                },
                dispatchEvent: function(a) {
                    a.target = null != this.__targetDispatcher ? this.__targetDispatcher : this;
                    return this.__dispatchEvent(a)
                },
                hasEventListener: function(a) {
                    return null == this.__eventMap ? !1 : Object.prototype.hasOwnProperty.call(this.__eventMap.h, a)
                },
                removeEventListener: function(a, b, c) {
                    null == c && (c = !1);
                    if (null != this.__eventMap && null != b) {
                        var d = this.__eventMap.h[a];
                        if (null != d) {
                            for (var f = this.__iterators.h[a], h = 0, k = d.length; h < k;) {
                                var n = h++;
                                if (d[n].match(b, c)) {
                                    for (b = 0; b < f.length;) c = f[b], ++b, c.remove(d[n], n);
                                    d.splice(n, 1);
                                    break
                                }
                            }
                            0 == d.length && (d = this.__eventMap, f = a, Object.prototype.hasOwnProperty.call(d.h, f) && delete d.h[f], d = this.__iterators,
                                f = a, Object.prototype.hasOwnProperty.call(d.h, f) && delete d.h[f]);
                            0 >= Object.keys(this.__eventMap.h).length && (this.__iterators = this.__eventMap = null)
                        }
                    }
                },
                __dispatchEvent: function(a) {
                    if (null == this.__eventMap || null == a) return !0;
                    var b = a.type,
                        c = this.__eventMap.h[b];
                    if (null == c) return !0;
                    null == a.target && (a.target = null != this.__targetDispatcher ? this.__targetDispatcher : this);
                    a.currentTarget = this;
                    var d = 1 == a.eventPhase;
                    b = this.__iterators.h[b];
                    var f = b[0];
                    f.active && (f = new Yg(c), b.push(f));
                    f.start();
                    for (var h = f; h.hasNext();) {
                        var k =
                            h.next();
                        if (null != k && k.useCapture == d) {
                            if (k.useWeakReference && null != k.weakRefCallback) {
                                var n = k.weakRefCallback.deref();
                                if (null == n) n = f.index - 1, c.splice(n, 1), f.remove(k, n);
                                else if (null != Ra.get_current() && null != Ra.get_current().stage && Ra.get_current().stage.__uncaughtErrorEvents.__enabled) try {
                                    n(a)
                                } catch (p) {
                                    Ta.lastError = p, k = X.caught(p).unwrap(), a instanceof eg || Ra.get_current().stage.__handleError(k)
                                } else n(a)
                            } else if (null != Ra.get_current() && null != Ra.get_current().stage && Ra.get_current().stage.__uncaughtErrorEvents.__enabled) try {
                                k.callback(a)
                            } catch (p) {
                                Ta.lastError =
                                    p, k = X.caught(p).unwrap(), a instanceof eg || Ra.get_current().stage.__handleError(k)
                            } else k.callback(a);
                            if (a.__isCanceledNow) break
                        }
                    }
                    f.stop();
                    f != b[0] ? N.remove(b, f) : f.reset(c);
                    return !a.isDefaultPrevented()
                },
                __addListenerByPriority: function(a, b) {
                    for (var c = a.length, d = c, f = 0; f < c;) {
                        var h = f++;
                        if (a[h].priority < b.priority) {
                            d = h;
                            break
                        }
                    }
                    a.splice(d, 0, b)
                },
                __class__: oa
            };
            var mb = function() {};
            g["openfl.display.IBitmapDrawable"] = mb;
            mb.__name__ = "openfl.display.IBitmapDrawable";
            mb.__isInterface__ = !0;
            mb.prototype = {
                __class__: mb
            };
            var la = {
                    toIntVector: function(a, b, c, d) {
                        return new Zg(b, c, d)
                    },
                    toFloatVector: function(a, b, c, d) {
                        return new Ie(b, c, d, !0)
                    },
                    toObjectVector: function(a, b, c, d) {
                        return new fg(b, c, d, !0)
                    }
                },
                nb = function(a, b, c) {
                    this.__pool = new pa;
                    this.inactiveObjects = this.activeObjects = 0;
                    this.__inactiveObject1 = this.__inactiveObject0 = null;
                    this.__inactiveObjectList = new ab;
                    null != a && (this.create = a);
                    null != b && (this.clean = b);
                    null != c && this.set_size(c)
                };
            g["lime.utils.ObjectPool"] = nb;
            nb.__name__ = "lime.utils.ObjectPool";
            nb.prototype = {
                clean: function(a) {},
                create: function() {
                    return null
                },
                get: function() {
                    var a = null;
                    if (0 < this.inactiveObjects) null != this.__inactiveObject0 ? (a = this.__inactiveObject0, this.__inactiveObject0 = null) : null != this.__inactiveObject1 ? (a = this.__inactiveObject1, this.__inactiveObject1 = null) : (a = this.__inactiveObjectList.pop(), 0 < this.__inactiveObjectList.length && (this.__inactiveObject0 = this.__inactiveObjectList.pop()), 0 < this.__inactiveObjectList.length && (this.__inactiveObject1 = this.__inactiveObjectList.pop())), this.inactiveObjects--, this.activeObjects++;
                    else if (null == this.__size || this.activeObjects < this.__size) a = this.create(), null != a && (this.__pool.set(a, !0), this.activeObjects++);
                    return a
                },
                release: function(a) {
                    this.activeObjects--;
                    null == this.__size || this.activeObjects + this.inactiveObjects < this.__size ? (this.clean(a), null == this.__inactiveObject0 ? this.__inactiveObject0 = a : null == this.__inactiveObject1 ? this.__inactiveObject1 = a : this.__inactiveObjectList.add(a), this.inactiveObjects++) : this.__pool.remove(a)
                },
                __removeInactive: function(a) {
                    if (!(0 >= a || 0 == this.inactiveObjects) &&
                        (null != this.__inactiveObject0 && (this.__pool.remove(this.__inactiveObject0), this.__inactiveObject0 = null, this.inactiveObjects--, --a), 0 != a && 0 != this.inactiveObjects && (null != this.__inactiveObject1 && (this.__pool.remove(this.__inactiveObject1), this.__inactiveObject1 = null, this.inactiveObjects--, --a), 0 != a && 0 != this.inactiveObjects)))
                        for (var b = this.__inactiveObjectList.h; null != b;) {
                            var c = b.item;
                            b = b.next;
                            this.__pool.remove(c);
                            this.__inactiveObjectList.remove(c);
                            this.inactiveObjects--;
                            --a;
                            if (0 == a || 0 == this.inactiveObjects) break
                        }
                },
                set_size: function(a) {
                    if (null == a) this.__size = null;
                    else {
                        var b = this.inactiveObjects + this.activeObjects;
                        this.__size = a;
                        if (b > a) this.__removeInactive(b - a);
                        else if (a > b)
                            for (var c = 0, d = a - b; c < d;)
                                if (c++, b = this.create(), null != b) this.__pool.set(b, !1), this.__inactiveObjectList.add(b), this.inactiveObjects++;
                                else break
                    }
                    return a
                },
                __class__: nb,
                __properties__: {
                    set_size: "set_size"
                }
            };
            var Y = function() {};
            g["haxe.IMap"] = Y;
            Y.__name__ = "haxe.IMap";
            Y.__isInterface__ = !0;
            Y.prototype = {
                __class__: Y
            };
            var pa = function() {
                this.h = {
                    __keys__: {}
                }
            };
            g["haxe.ds.ObjectMap"] = pa;
            pa.__name__ = "haxe.ds.ObjectMap";
            pa.__interfaces__ = [Y];
            pa.prototype = {
                set: function(a, b) {
                    var c = a.__id__;
                    null == c && (c = a.__id__ = E.$haxeUID++);
                    this.h[c] = b;
                    this.h.__keys__[c] = a
                },
                get: function(a) {
                    return this.h[a.__id__]
                },
                remove: function(a) {
                    a = a.__id__;
                    if (null == this.h.__keys__[a]) return !1;
                    delete this.h[a];
                    delete this.h.__keys__[a];
                    return !0
                },
                keys: function() {
                    var a = [],
                        b;
                    for (b in this.h.__keys__) this.h.hasOwnProperty(b) && a.push(this.h.__keys__[b]);
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
                            return this.ref[a.__id__]
                        }
                    }
                },
                __class__: pa
            };
            var ab = function() {
                this.length = 0
            };
            g["haxe.ds.List"] = ab;
            ab.__name__ = "haxe.ds.List";
            ab.prototype = {
                add: function(a) {
                    a = new Mh(a, null);
                    null == this.h ? this.h = a : this.q.next = a;
                    this.q = a;
                    this.length++
                },
                push: function(a) {
                    this.h = a = new Mh(a, this.h);
                    null == this.q && (this.q = a);
                    this.length++
                },
                pop: function() {
                    if (null == this.h) return null;
                    var a = this.h.item;
                    this.h = this.h.next;
                    null == this.h &&
                        (this.q = null);
                    this.length--;
                    return a
                },
                isEmpty: function() {
                    return null == this.h
                },
                clear: function() {
                    this.q = this.h = null;
                    this.length = 0
                },
                remove: function(a) {
                    for (var b = null, c = this.h; null != c;) {
                        if (c.item == a) return null == b ? this.h = c.next : b.next = c.next, this.q == c && (this.q = b), this.length--, !0;
                        b = c;
                        c = c.next
                    }
                    return !1
                },
                iterator: function() {
                    return new Uj(this.h)
                },
                __class__: ab
            };
            var S = function() {
                oa.call(this);
                this.__alpha = this.__drawableType = 1;
                this.__blendMode = 10;
                this.__cacheAsBitmap = !1;
                this.__transform = new ua;
                this.__visible = !0;
                this.__rotationSine = this.__rotation = 0;
                this.__worldAlpha = this.__scaleY = this.__scaleX = this.__rotationCosine = 1;
                this.__worldBlendMode = 10;
                this.__worldTransform = new ua;
                this.__worldColorTransform = new Tb;
                this.__renderTransform = new ua;
                this.__worldVisible = !0;
                this.set_name("instance" + ++S.__instanceCount);
                null != S.__initStage && (this.stage = S.__initStage, S.__initStage = null, this.stage.addChild(this))
            };
            g["openfl.display.DisplayObject"] = S;
            S.__name__ = "openfl.display.DisplayObject";
            S.__interfaces__ = [mb];
            S.__super__ =
                oa;
            S.prototype = v(oa.prototype, {
                addEventListener: function(a, b, c, d, f) {
                    null == f && (f = !1);
                    null == d && (d = 0);
                    null == c && (c = !1);
                    switch (a) {
                        case "activate":
                        case "deactivate":
                        case "enterFrame":
                        case "exitFrame":
                        case "frameConstructed":
                        case "render":
                            Object.prototype.hasOwnProperty.call(S.__broadcastEvents.h, a) || (S.__broadcastEvents.h[a] = []);
                            var h = S.__broadcastEvents.h[a]; - 1 == h.indexOf(this) && h.push(this);
                            break;
                        case "clearDOM":
                        case "renderCairo":
                        case "renderCanvas":
                        case "renderDOM":
                        case "renderOpenGL":
                            null == this.__customRenderEvent &&
                                (this.__customRenderEvent = new Nh(null), this.__customRenderEvent.objectColorTransform = new Tb, this.__customRenderEvent.objectMatrix = new ua, this.__customRenderClear = !0)
                    }
                    oa.prototype.addEventListener.call(this, a, b, c, d, f)
                },
                dispatchEvent: function(a) {
                    if (a instanceof Ob) {
                        var b = this.__getRenderTransform();
                        a.stageX = a.localX * b.a + a.localY * b.c + b.tx;
                        b = this.__getRenderTransform();
                        a.stageY = a.localX * b.b + a.localY * b.d + b.ty
                    } else a instanceof Zd && (b = this.__getRenderTransform(), a.stageX = a.localX * b.a + a.localY * b.c + b.tx,
                        b = this.__getRenderTransform(), a.stageY = a.localX * b.b + a.localY * b.d + b.ty);
                    a.target = this;
                    return this.__dispatchWithCapture(a)
                },
                getBounds: function(a) {
                    var b = ua.__pool.get();
                    if (null != a && a != this) {
                        b.copyFrom(this.__getWorldTransform());
                        var c = ua.__pool.get();
                        c.copyFrom(a.__getWorldTransform());
                        c.invert();
                        b.concat(c);
                        ua.__pool.release(c)
                    } else b.identity();
                    a = new na;
                    this.__getBounds(a, b);
                    ua.__pool.release(b);
                    return a
                },
                getRect: function(a) {
                    return this.getBounds(a)
                },
                globalToLocal: function(a) {
                    return this.__globalToLocal(a,
                        new I)
                },
                hitTestPoint: function(a, b, c) {
                    null == c && (c = !1);
                    return null != this.stage ? this.__hitTest(a, b, c, null, !1, this) : !1
                },
                localToGlobal: function(a) {
                    return this.__getRenderTransform().transformPoint(a)
                },
                removeEventListener: function(a, b, c) {
                    null == c && (c = !1);
                    oa.prototype.removeEventListener.call(this, a, b, c);
                    switch (a) {
                        case "activate":
                        case "deactivate":
                        case "enterFrame":
                        case "exitFrame":
                        case "frameConstructed":
                        case "render":
                            this.hasEventListener(a) || Object.prototype.hasOwnProperty.call(S.__broadcastEvents.h, a) &&
                                N.remove(S.__broadcastEvents.h[a], this);
                            break;
                        case "clearDOM":
                        case "renderCairo":
                        case "renderCanvas":
                        case "renderDOM":
                        case "renderOpenGL":
                            this.hasEventListener("clearDOM") || this.hasEventListener("renderCairo") || this.hasEventListener("renderCanvas") || this.hasEventListener("renderDOM") || this.hasEventListener("renderOpenGL") || (this.__customRenderEvent = null)
                    }
                },
                __cleanup: function() {
                    this.__context = this.__canvas = this.__cairo = null;
                    null != this.__graphics && this.__graphics.__cleanup();
                    null != this.__cacheBitmap &&
                        (this.__cacheBitmap.__cleanup(), this.__cacheBitmap = null);
                    null != this.__cacheBitmapData && (this.__cacheBitmapData.dispose(), this.__cacheBitmapData = null)
                },
                __dispatch: function(a) {
                    if (null != this.__eventMap && this.hasEventListener(a.type)) {
                        var b = oa.prototype.__dispatchEvent.call(this, a);
                        return a.__isCanceled ? !0 : b
                    }
                    return !0
                },
                __dispatchChildren: function(a) {},
                __dispatchEvent: function(a) {
                    var b = a.bubbles ? this.parent : null,
                        c = oa.prototype.__dispatchEvent.call(this, a);
                    if (a.__isCanceled) return !0;
                    null != b && b != this && (a.eventPhase =
                        3, null == a.target && (a.target = this), b.__dispatchEvent(a));
                    return c
                },
                __dispatchWithCapture: function(a) {
                    null == a.target && (a.target = this);
                    if (null != this.parent)
                        if (a.eventPhase = 1, this.parent == this.stage) this.parent.__dispatch(a);
                        else {
                            for (var b = S.__tempStack.get(), c = this.parent, d = 0; null != c;) b.set(d, c), c = c.parent, ++d;
                            c = 0;
                            for (var f = d; c < f;) {
                                var h = c++;
                                b.get(d - h - 1).__dispatch(a)
                            }
                            S.__tempStack.release(b)
                        } a.eventPhase = 2;
                    return this.__dispatchEvent(a)
                },
                __enterFrame: function(a) {},
                __getBounds: function(a, b) {
                    null != this.__graphics &&
                        this.__graphics.__getBounds(a, b)
                },
                __getCursor: function() {
                    return null
                },
                __getFilterBounds: function(a, b) {
                    this.__getRenderBounds(a, b);
                    if (null != this.__filters) {
                        b = na.__pool.get();
                        for (var c = 0, d = this.__filters; c < d.length;) {
                            var f = d[c];
                            ++c;
                            b.__expand(-f.__leftExtension, -f.__topExtension, f.__leftExtension + f.__rightExtension, f.__topExtension + f.__bottomExtension)
                        }
                        a.width += b.width;
                        a.height += b.height;
                        a.x += b.x;
                        a.y += b.y;
                        na.__pool.release(b)
                    }
                },
                __getInteractive: function(a) {
                    return !1
                },
                __getLocalBounds: function(a) {
                    this.__getBounds(a,
                        this.__transform);
                    a.x -= this.__transform.tx;
                    a.y -= this.__transform.ty
                },
                __getRenderBounds: function(a, b) {
                    if (null == this.__scrollRect) this.__getBounds(a, b);
                    else {
                        var c = na.__pool.get();
                        c.copyFrom(this.__scrollRect);
                        c.__transform(c, b);
                        a.__expand(c.x, c.y, c.width, c.height);
                        na.__pool.release(c)
                    }
                },
                __getRenderTransform: function() {
                    this.__getWorldTransform();
                    return this.__renderTransform
                },
                __getWorldTransform: function() {
                    if (this.__transformDirty || this.__worldTransformInvalid) {
                        var a = [],
                            b = this;
                        if (null == this.parent) this.__update(!0,
                            !1);
                        else
                            for (; b != this.stage && (a.push(b), b = b.parent, null != b););
                        for (var c = a.length; 0 <= --c;) b = a[c], b.__update(!0, !1)
                    }
                    return this.__worldTransform
                },
                __globalToLocal: function(a, b) {
                    this.__getRenderTransform();
                    if (a == b) {
                        var c = this.__renderTransform,
                            d = c.a * c.d - c.b * c.c;
                        if (0 == d) a.x = -c.tx, a.y = -c.ty;
                        else {
                            var f = 1 / d * (c.c * (c.ty - a.y) + c.d * (a.x - c.tx));
                            a.y = 1 / d * (c.a * (a.y - c.ty) + c.b * (c.tx - a.x));
                            a.x = f
                        }
                    } else c = this.__renderTransform, d = c.a * c.d - c.b * c.c, b.x = 0 == d ? -c.tx : 1 / d * (c.c * (c.ty - a.y) + c.d * (a.x - c.tx)), c = this.__renderTransform,
                        d = c.a * c.d - c.b * c.c, b.y = 0 == d ? -c.ty : 1 / d * (c.a * (a.y - c.ty) + c.b * (c.tx - a.x));
                    return b
                },
                __hitTest: function(a, b, c, d, f, h) {
                    if (null != this.__graphics) {
                        if (!h.__visible || this.__isMask || null != this.get_mask() && !this.get_mask().__hitTestMask(a, b)) return !1;
                        if (this.__graphics.__hitTest(a, b, c, this.__getRenderTransform())) return null == d || f || d.push(h), !0
                    }
                    return !1
                },
                __hitTestMask: function(a, b) {
                    return null != this.__graphics && this.__graphics.__hitTest(a, b, !0, this.__getRenderTransform()) ? !0 : !1
                },
                __readGraphicsData: function(a, b) {
                    null !=
                        this.__graphics && this.__graphics.__readGraphicsData(a)
                },
                __setParentRenderDirty: function() {
                    var a = null != this.__renderParent ? this.__renderParent : this.parent;
                    null == a || a.__renderDirty || (a.__renderDirty = !0, a.__setParentRenderDirty())
                },
                __setStageReference: function(a) {
                    this.stage = a
                },
                __setTransformDirty: function() {
                    this.__transformDirty || (this.__transformDirty = !0, this.__setWorldTransformInvalid(), this.__setParentRenderDirty())
                },
                __setWorldTransformInvalid: function() {
                    this.__worldTransformInvalid = !0
                },
                __update: function(a,
                    b) {
                    var c = null != this.__renderParent ? this.__renderParent : this.parent;
                    this.__isMask && null == c && (c = this.__maskTarget);
                    this.__renderable = this.__visible && 0 != this.__scaleX && 0 != this.__scaleY && !this.__isMask && (null == c || !c.__isMask);
                    this.__updateTransforms();
                    this.__worldTransformInvalid = this.__transformDirty = !1;
                    if (!a)
                        if (S.__supportDOM && (this.__renderTransformChanged = !this.__renderTransform.equals(this.__renderTransformCache), null == this.__renderTransformCache ? this.__renderTransformCache = this.__renderTransform.clone() :
                                this.__renderTransformCache.copyFrom(this.__renderTransform)), null != c) {
                            if (S.__supportDOM) {
                                var d = c.__worldVisible && this.__visible;
                                this.__worldVisibleChanged = this.__worldVisible != d;
                                this.__worldVisible = d
                            }
                            d = this.get_alpha() * c.__worldAlpha;
                            this.__worldAlphaChanged = this.__worldAlpha != d;
                            this.__worldAlpha = d;
                            null != this.__objectTransform ? (this.__worldColorTransform.__copyFrom(this.__objectTransform.__colorTransform), this.__worldColorTransform.__combine(c.__worldColorTransform)) : this.__worldColorTransform.__copyFrom(c.__worldColorTransform);
                            this.__worldBlendMode = null == this.__blendMode || 10 == this.__blendMode ? c.__worldBlendMode : this.__blendMode;
                            this.__worldShader = null == this.__shader ? c.__shader : this.__shader;
                            this.__worldScale9Grid = null == this.__scale9Grid ? c.__scale9Grid : this.__scale9Grid
                        } else this.__worldAlpha = this.get_alpha(), S.__supportDOM && (this.__worldVisibleChanged = this.__worldVisible != this.__visible, this.__worldVisible = this.__visible), this.__worldAlphaChanged = this.__worldAlpha != this.get_alpha(), null != this.__objectTransform ? this.__worldColorTransform.__copyFrom(this.__objectTransform.__colorTransform) :
                            this.__worldColorTransform.__identity(), this.__worldBlendMode = this.__blendMode, this.__worldShader = this.__shader, this.__worldScale9Grid = this.__scale9Grid;
                    b && null != this.get_mask() && this.get_mask().__update(a, !0)
                },
                __updateTransforms: function(a) {
                    var b = null != a;
                    a = b ? a : this.__transform;
                    null == this.__worldTransform && (this.__worldTransform = new ua);
                    null == this.__renderTransform && (this.__renderTransform = new ua);
                    var c = null != this.__renderParent ? this.__renderParent : this.parent;
                    if (b || null == this.parent) this.__worldTransform.copyFrom(a);
                    else {
                        var d = this.parent.__worldTransform,
                            f = this.__worldTransform;
                        f.a = a.a * d.a + a.b * d.c;
                        f.b = a.a * d.b + a.b * d.d;
                        f.c = a.c * d.a + a.d * d.c;
                        f.d = a.c * d.b + a.d * d.d;
                        f.tx = a.tx * d.a + a.ty * d.c + d.tx;
                        f.ty = a.tx * d.b + a.ty * d.d + d.ty
                    }
                    b || null == c ? this.__renderTransform.copyFrom(a) : (d = c.__renderTransform, f = this.__renderTransform, f.a = a.a * d.a + a.b * d.c, f.b = a.a * d.b + a.b * d.d, f.c = a.c * d.a + a.d * d.c, f.d = a.c * d.b + a.d * d.d, f.tx = a.tx * d.a + a.ty * d.c + d.tx, f.ty = a.tx * d.b + a.ty * d.d + d.ty);
                    null != this.__scrollRect && (b = this.__renderTransform, a = -this.__scrollRect.x,
                        c = -this.__scrollRect.y, b.tx = a * b.a + c * b.c + b.tx, b.ty = a * b.b + c * b.d + b.ty)
                },
                get_alpha: function() {
                    return this.__alpha
                },
                set_alpha: function(a) {
                    1 < a && (a = 1);
                    0 > a && (a = 0);
                    a == this.__alpha || this.get_cacheAsBitmap() || this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty());
                    return this.__alpha = a
                },
                get_blendMode: function() {
                    return this.__blendMode
                },
                get_cacheAsBitmap: function() {
                    return null == this.__filters ? this.__cacheAsBitmap : !0
                },
                set_cacheAsBitmap: function(a) {
                    a == this.__cacheAsBitmap || this.__renderDirty ||
                        (this.__renderDirty = !0, this.__setParentRenderDirty());
                    return this.__cacheAsBitmap = a
                },
                get_filters: function() {
                    return null == this.__filters ? [] : this.__filters.slice()
                },
                set_filters: function(a) {
                    if (null != a && 0 < a.length) {
                        for (var b = [], c = 0; c < a.length;) {
                            var d = a[c];
                            ++c;
                            d = d.clone();
                            d.__renderDirty = !0;
                            b.push(d)
                        }
                        this.__filters = b;
                        this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty())
                    } else null != this.__filters && (this.__filters = null, this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
                    return a
                },
                get_height: function() {
                    var a = na.__pool.get();
                    this.__getLocalBounds(a);
                    var b = a.height;
                    na.__pool.release(a);
                    return b
                },
                set_height: function(a) {
                    var b = na.__pool.get(),
                        c = ua.__pool.get();
                    c.identity();
                    this.__getBounds(b, c);
                    a != b.height ? this.set_scaleY(a / b.height) : this.set_scaleY(1);
                    na.__pool.release(b);
                    ua.__pool.release(c);
                    return a
                },
                get_loaderInfo: function() {
                    return null != this.stage ? Dc.current.__loaderInfo : null
                },
                get_mask: function() {
                    return this.__mask
                },
                set_mask: function(a) {
                    if (a == this.__mask) return a;
                    a != this.__mask && (this.__setTransformDirty(), this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty()));
                    if (null != this.__mask) {
                        this.__mask.__isMask = !1;
                        this.__mask.__maskTarget = null;
                        this.__mask.__setTransformDirty();
                        var b = this.__mask;
                        b.__renderDirty || (b.__renderDirty = !0, b.__setParentRenderDirty())
                    }
                    null != a && (a.__isMask = !0, a.__maskTarget = this, a.__setWorldTransformInvalid());
                    null != this.__cacheBitmap && this.__cacheBitmap.get_mask() != a && this.__cacheBitmap.set_mask(a);
                    return this.__mask = a
                },
                get_mouseX: function() {
                    var a = null != this.stage ? this.stage.__mouseX : Dc.current.stage.__mouseX,
                        b = null != this.stage ? this.stage.__mouseY : Dc.current.stage.__mouseY,
                        c = this.__getRenderTransform(),
                        d = c.a * c.d - c.b * c.c;
                    return 0 == d ? -c.tx : 1 / d * (c.c * (c.ty - b) + c.d * (a - c.tx))
                },
                get_mouseY: function() {
                    var a = null != this.stage ? this.stage.__mouseX : Dc.current.stage.__mouseX,
                        b = null != this.stage ? this.stage.__mouseY : Dc.current.stage.__mouseY,
                        c = this.__getRenderTransform(),
                        d = c.a * c.d - c.b * c.c;
                    return 0 == d ? -c.ty : 1 / d * (c.a * (b - c.ty) + c.b * (c.tx -
                        a))
                },
                get_name: function() {
                    return this.__name
                },
                set_name: function(a) {
                    return this.__name = a
                },
                get_root: function() {
                    return null != this.stage ? Dc.current : null
                },
                get_rotation: function() {
                    return this.__rotation
                },
                set_rotation: function(a) {
                    if (a != this.__rotation) {
                        a %= 360;
                        180 < a ? a -= 360 : -180 > a && (a += 360);
                        this.__rotation = a;
                        var b = Math.PI / 180 * this.__rotation;
                        this.__rotationSine = Math.sin(b);
                        this.__rotationCosine = Math.cos(b);
                        this.__transform.a = this.__rotationCosine * this.__scaleX;
                        this.__transform.b = this.__rotationSine * this.__scaleX;
                        this.__transform.c = -this.__rotationSine * this.__scaleY;
                        this.__transform.d = this.__rotationCosine * this.__scaleY;
                        this.__setTransformDirty()
                    }
                    return a
                },
                get_scaleX: function() {
                    return this.__scaleX
                },
                set_scaleX: function(a) {
                    if (a != this.__scaleX)
                        if (this.__scaleX = a, 0 == this.__transform.b) a != this.__transform.a && this.__setTransformDirty(), this.__transform.a = a;
                        else {
                            var b = this.__rotationCosine * a,
                                c = this.__rotationSine * a;
                            this.__transform.a == b && this.__transform.b == c || this.__setTransformDirty();
                            this.__transform.a = b;
                            this.__transform.b =
                                c
                        } return a
                },
                get_scaleY: function() {
                    return this.__scaleY
                },
                set_scaleY: function(a) {
                    if (a != this.__scaleY)
                        if (this.__scaleY = a, 0 == this.__transform.c) a != this.__transform.d && this.__setTransformDirty(), this.__transform.d = a;
                        else {
                            var b = -this.__rotationSine * a,
                                c = this.__rotationCosine * a;
                            this.__transform.d == c && this.__transform.c == b || this.__setTransformDirty();
                            this.__transform.c = b;
                            this.__transform.d = c
                        } return a
                },
                get_scrollRect: function() {
                    return null == this.__scrollRect ? null : this.__scrollRect.clone()
                },
                get_transform: function() {
                    null ==
                        this.__objectTransform && (this.__objectTransform = new Oh(this));
                    return this.__objectTransform
                },
                set_transform: function(a) {
                    if (null == a) throw new Cf("Parameter transform must be non-null.");
                    null == this.__objectTransform && (this.__objectTransform = new Oh(this));
                    this.__setTransformDirty();
                    this.__objectTransform.set_matrix(a.get_matrix());
                    if (!this.__objectTransform.__colorTransform.__equals(a.__colorTransform, !0) || !this.get_cacheAsBitmap() && this.__objectTransform.__colorTransform.alphaMultiplier != a.__colorTransform.alphaMultiplier) this.__objectTransform.__colorTransform.__copyFrom(a.get_colorTransform()),
                        this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty());
                    return this.__objectTransform
                },
                get_visible: function() {
                    return this.__visible
                },
                set_visible: function(a) {
                    a == this.__visible || this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty());
                    return this.__visible = a
                },
                get_width: function() {
                    var a = na.__pool.get();
                    this.__getLocalBounds(a);
                    var b = a.width;
                    na.__pool.release(a);
                    return b
                },
                set_width: function(a) {
                    var b = na.__pool.get(),
                        c = ua.__pool.get();
                    c.identity();
                    this.__getBounds(b, c);
                    a != b.width ? this.set_scaleX(a / b.width) : this.set_scaleX(1);
                    na.__pool.release(b);
                    ua.__pool.release(c);
                    return a
                },
                get_x: function() {
                    return this.__transform.tx
                },
                set_x: function(a) {
                    a != this.__transform.tx && this.__setTransformDirty();
                    return this.__transform.tx = a
                },
                get_y: function() {
                    return this.__transform.ty
                },
                set_y: function(a) {
                    a != this.__transform.ty && this.__setTransformDirty();
                    return this.__transform.ty = a
                },
                __class__: S,
                __properties__: {
                    set_y: "set_y",
                    get_y: "get_y",
                    set_x: "set_x",
                    get_x: "get_x",
                    set_width: "set_width",
                    get_width: "get_width",
                    set_visible: "set_visible",
                    get_visible: "get_visible",
                    set_transform: "set_transform",
                    get_transform: "get_transform",
                    get_scrollRect: "get_scrollRect",
                    set_scaleY: "set_scaleY",
                    get_scaleY: "get_scaleY",
                    set_scaleX: "set_scaleX",
                    get_scaleX: "get_scaleX",
                    set_rotation: "set_rotation",
                    get_rotation: "get_rotation",
                    get_root: "get_root",
                    set_name: "set_name",
                    get_name: "get_name",
                    get_mouseY: "get_mouseY",
                    get_mouseX: "get_mouseX",
                    set_mask: "set_mask",
                    get_mask: "get_mask",
                    get_loaderInfo: "get_loaderInfo",
                    set_height: "set_height",
                    get_height: "get_height",
                    set_filters: "set_filters",
                    get_filters: "get_filters",
                    set_cacheAsBitmap: "set_cacheAsBitmap",
                    get_cacheAsBitmap: "get_cacheAsBitmap",
                    get_blendMode: "get_blendMode",
                    set_alpha: "set_alpha",
                    get_alpha: "get_alpha"
                }
            });
            var xa = function() {
                S.call(this);
                this.doubleClickEnabled = !1;
                this.mouseEnabled = !0;
                this.needsSoftKeyboard = !1;
                this.__tabEnabled = null;
                this.__tabIndex = -1
            };
            g["openfl.display.InteractiveObject"] = xa;
            xa.__name__ = "openfl.display.InteractiveObject";
            xa.__super__ = S;
            xa.prototype = v(S.prototype, {
                __allowMouseFocus: function() {
                    return this.mouseEnabled ? this.get_tabEnabled() : !1
                },
                __getInteractive: function(a) {
                    null != a && (a.push(this), null != this.parent && this.parent.__getInteractive(a));
                    return !0
                },
                __hitTest: function(a, b, c, d, f, h) {
                    return !h.get_visible() || this.__isMask || f && !this.mouseEnabled ? !1 : S.prototype.__hitTest.call(this, a, b, c, d, f, h)
                },
                __tabTest: function(a) {
                    this.get_tabEnabled() && a.push(this)
                },
                get_tabEnabled: function() {
                    return 1 == this.__tabEnabled ? !0 : !1
                },
                get_tabIndex: function() {
                    return this.__tabIndex
                },
                __class__: xa,
                __properties__: v(S.prototype.__properties__, {
                    get_tabIndex: "get_tabIndex",
                    get_tabEnabled: "get_tabEnabled"
                })
            });
            var kb = function() {
                xa.call(this);
                this.__tabChildren = this.mouseChildren = !0;
                this.__children = [];
                this.__removedChildren = la.toObjectVector(null)
            };
            g["openfl.display.DisplayObjectContainer"] = kb;
            kb.__name__ = "openfl.display.DisplayObjectContainer";
            kb.__super__ = xa;
            kb.prototype = v(xa.prototype, {
                addChild: function(a) {
                    return this.addChildAt(a, this.get_numChildren())
                },
                addChildAt: function(a, b) {
                    if (null ==
                        a) throw a = new Cf("Error #2007: Parameter child must be non-null."), a.errorID = 2007, a;
                    if (a == this) throw a = new gg("Error #2024: An object cannot be added as a child of itself."), a.errorID = 2024, a;
                    if (a.stage == a) throw a = new gg("Error #3783: A Stage object cannot be added as the child of another object."), a.errorID = 3783, a;
                    if (b > this.__children.length || 0 > b) throw X.thrown("Invalid index position " + b);
                    if (a.parent == this) this.__children[b] != a && (N.remove(this.__children, a), this.__children.splice(b, 0, a), this.__renderDirty ||
                        (this.__renderDirty = !0, this.__setParentRenderDirty()));
                    else {
                        null != a.parent && a.parent.removeChild(a);
                        this.__children.splice(b, 0, a);
                        a.parent = this;
                        (b = null != this.stage && null == a.stage) && a.__setStageReference(this.stage);
                        a.__setTransformDirty();
                        a.__renderDirty || (a.__renderDirty = !0, a.__setParentRenderDirty());
                        this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty());
                        var c = new wa("added");
                        c.bubbles = !0;
                        c.target = a;
                        a.__dispatchWithCapture(c);
                        b && (c = new wa("addedToStage", !1, !1), a.__dispatchWithCapture(c),
                            a.__dispatchChildren(c))
                    }
                    return a
                },
                getChildAt: function(a) {
                    return 0 <= a && a < this.__children.length ? this.__children[a] : null
                },
                getChildByName: function(a) {
                    for (var b = 0, c = this.__children; b < c.length;) {
                        var d = c[b];
                        ++b;
                        if (d.get_name() == a) return d
                    }
                    return null
                },
                getChildIndex: function(a) {
                    for (var b = 0, c = this.__children.length; b < c;) {
                        var d = b++;
                        if (this.__children[d] == a) return d
                    }
                    return -1
                },
                removeChild: function(a) {
                    if (null != a && a.parent == this) {
                        a.__setTransformDirty();
                        a.__renderDirty || (a.__renderDirty = !0, a.__setParentRenderDirty());
                        this.__renderDirty || (this.__renderDirty = !0, this.__setParentRenderDirty());
                        var b = new wa("removed", !0);
                        a.__dispatchWithCapture(b);
                        null != this.stage && (null != a.stage && this.stage.get_focus() == a && this.stage.set_focus(null), b = new wa("removedFromStage", !1, !1), a.__dispatchWithCapture(b), a.__dispatchChildren(b), a.__setStageReference(null));
                        a.parent = null;
                        N.remove(this.__children, a);
                        this.__removedChildren.push(a);
                        a.__setTransformDirty()
                    }
                    return a
                },
                removeChildAt: function(a) {
                    return 0 <= a && a < this.__children.length ?
                        this.removeChild(this.__children[a]) : null
                },
                removeChildren: function(a, b) {
                    null == b && (b = 2147483647);
                    null == a && (a = 0);
                    if (2147483647 == b && (b = this.__children.length - 1, 0 > b)) return;
                    if (!(a > this.__children.length - 1)) {
                        if (b < a || 0 > a || b > this.__children.length) throw new $g("The supplied index is out of bounds.");
                        for (b -= a; 0 <= b;) this.removeChildAt(a), --b
                    }
                },
                __cleanup: function() {
                    xa.prototype.__cleanup.call(this);
                    for (var a = 0, b = this.__children; a < b.length;) {
                        var c = b[a];
                        ++a;
                        c.__cleanup()
                    }
                    for (a = this.__removedChildren.iterator(); a.hasNext();) b =
                        a.next(), null == b.stage && b.__cleanup();
                    this.__removedChildren.set_length(0)
                },
                __dispatchChildren: function(a) {
                    if (null != this.__children)
                        for (var b = 0, c = this.__children; b < c.length;) {
                            var d = c[b];
                            ++b;
                            a.target = d;
                            if (!d.__dispatchWithCapture(a)) break;
                            d.__dispatchChildren(a)
                        }
                },
                __enterFrame: function(a) {
                    for (var b = 0, c = this.__children; b < c.length;) {
                        var d = c[b];
                        ++b;
                        d.__enterFrame(a)
                    }
                },
                __getBounds: function(a, b) {
                    xa.prototype.__getBounds.call(this, a, b);
                    if (0 != this.__children.length) {
                        for (var c = ua.__pool.get(), d = 0, f = this.__children; d <
                            f.length;) {
                            var h = f[d];
                            ++d;
                            if (0 != h.__scaleX && 0 != h.__scaleY) {
                                var k = h.__transform;
                                c.a = k.a * b.a + k.b * b.c;
                                c.b = k.a * b.b + k.b * b.d;
                                c.c = k.c * b.a + k.d * b.c;
                                c.d = k.c * b.b + k.d * b.d;
                                c.tx = k.tx * b.a + k.ty * b.c + b.tx;
                                c.ty = k.tx * b.b + k.ty * b.d + b.ty;
                                h.__getBounds(a, c)
                            }
                        }
                        ua.__pool.release(c)
                    }
                },
                __getFilterBounds: function(a, b) {
                    xa.prototype.__getFilterBounds.call(this, a, b);
                    if (null == this.__scrollRect && 0 != this.__children.length) {
                        for (var c = ua.__pool.get(), d = 0, f = this.__children; d < f.length;) {
                            var h = f[d];
                            ++d;
                            if (0 != h.__scaleX && 0 != h.__scaleY && !h.__isMask) {
                                var k =
                                    h.__transform;
                                c.a = k.a * b.a + k.b * b.c;
                                c.b = k.a * b.b + k.b * b.d;
                                c.c = k.c * b.a + k.d * b.c;
                                c.d = k.c * b.b + k.d * b.d;
                                c.tx = k.tx * b.a + k.ty * b.c + b.tx;
                                c.ty = k.tx * b.b + k.ty * b.d + b.ty;
                                k = na.__pool.get();
                                h.__getFilterBounds(k, c);
                                a.__expand(k.x, k.y, k.width, k.height);
                                na.__pool.release(k)
                            }
                        }
                        ua.__pool.release(c)
                    }
                },
                __getRenderBounds: function(a, b) {
                    if (null != this.__scrollRect) xa.prototype.__getRenderBounds.call(this, a, b);
                    else if (xa.prototype.__getBounds.call(this, a, b), 0 != this.__children.length) {
                        for (var c = ua.__pool.get(), d = 0, f = this.__children; d <
                            f.length;) {
                            var h = f[d];
                            ++d;
                            if (0 != h.__scaleX && 0 != h.__scaleY && !h.__isMask) {
                                var k = h.__transform;
                                c.a = k.a * b.a + k.b * b.c;
                                c.b = k.a * b.b + k.b * b.d;
                                c.c = k.c * b.a + k.d * b.c;
                                c.d = k.c * b.b + k.d * b.d;
                                c.tx = k.tx * b.a + k.ty * b.c + b.tx;
                                c.ty = k.tx * b.b + k.ty * b.d + b.ty;
                                h.__getRenderBounds(a, c)
                            }
                        }
                        ua.__pool.release(c)
                    }
                },
                __hitTest: function(a, b, c, d, f, h) {
                    if (!h.get_visible() || this.__isMask || f && !this.mouseEnabled && !this.mouseChildren || null != this.get_mask() && !this.get_mask().__hitTestMask(a, b)) return !1;
                    if (null != this.__scrollRect) {
                        var k = I.__pool.get();
                        k.setTo(a, b);
                        var n = this.__getRenderTransform(),
                            p = n.a * n.d - n.b * n.c;
                        if (0 == p) k.x = -n.tx, k.y = -n.ty;
                        else {
                            var P = 1 / p * (n.c * (n.ty - k.y) + n.d * (k.x - n.tx));
                            k.y = 1 / p * (n.a * (k.y - n.ty) + n.b * (n.tx - k.x));
                            k.x = P
                        }
                        if (!this.__scrollRect.containsPoint(k)) return I.__pool.release(k), !1;
                        I.__pool.release(k)
                    }
                    k = this.__children.length;
                    if (f)
                        if (null == d || !this.mouseChildren)
                            for (; 0 <= --k;) {
                                if (this.__children[k].__hitTest(a, b, c, null, !0, this.__children[k])) return null != d && d.push(h), !0
                            } else {
                                if (null != d) {
                                    f = d.length;
                                    for (p = !1; 0 <= --k && !(((n = this.__children[k].__getInteractive(null)) ||
                                            this.mouseEnabled && !p) && this.__children[k].__hitTest(a, b, c, d, !0, this.__children[k]) && (p = !0, n && d.length > f)););
                                    if (p) return d.splice(f, 0, h), !0
                                }
                            } else {
                                for (p = !1; 0 <= --k && (!this.__children[k].__hitTest(a, b, c, d, !1, this.__children[k]) || (p = !0, null != d)););
                                return p
                            }
                    return !1
                },
                __hitTestMask: function(a, b) {
                    for (var c = this.__children.length; 0 <= --c;)
                        if (this.__children[c].__hitTestMask(a, b)) return !0;
                    return !1
                },
                __readGraphicsData: function(a, b) {
                    xa.prototype.__readGraphicsData.call(this, a, b);
                    if (b)
                        for (var c = 0, d = this.__children; c <
                            d.length;) {
                            var f = d[c];
                            ++c;
                            f.__readGraphicsData(a, b)
                        }
                },
                __setStageReference: function(a) {
                    xa.prototype.__setStageReference.call(this, a);
                    if (null != this.__children)
                        for (var b = 0, c = this.__children; b < c.length;) {
                            var d = c[b];
                            ++b;
                            d.__setStageReference(a)
                        }
                },
                __setWorldTransformInvalid: function() {
                    if (!this.__worldTransformInvalid && (this.__worldTransformInvalid = !0, null != this.__children))
                        for (var a = 0, b = this.__children; a < b.length;) {
                            var c = b[a];
                            ++a;
                            c.__setWorldTransformInvalid()
                        }
                },
                __tabTest: function(a) {
                    xa.prototype.__tabTest.call(this,
                        a);
                    if (this.get_tabChildren())
                        for (var b, c = 0, d = this.__children; c < d.length;) {
                            var f = d[c];
                            ++c;
                            if (b = f.__getInteractive(null)) b = f, b.__tabTest(a)
                        }
                },
                __update: function(a, b) {
                    xa.prototype.__update.call(this, a, b);
                    if (b) {
                        b = 0;
                        for (var c = this.__children; b < c.length;) {
                            var d = c[b];
                            ++b;
                            d.__update(a, !0)
                        }
                    }
                },
                get_numChildren: function() {
                    return this.__children.length
                },
                get_tabChildren: function() {
                    return this.__tabChildren
                },
                __class__: kb,
                __properties__: v(xa.prototype.__properties__, {
                    get_tabChildren: "get_tabChildren",
                    get_numChildren: "get_numChildren"
                })
            });
            var ka = function() {
                kb.call(this);
                this.__drawableType = 4;
                this.__buttonMode = !1;
                this.useHandCursor = !0;
                if (null != this.__pendingBindLibrary) {
                    var a = this.__pendingBindLibrary,
                        b = this.__pendingBindClassName;
                    this.__pendingBindClassName = this.__pendingBindLibrary = null;
                    a.bind(b, this)
                } else null != ka.__constructor && (a = ka.__constructor, ka.__constructor = null, a(this))
            };
            g["openfl.display.Sprite"] = ka;
            ka.__name__ = "openfl.display.Sprite";
            ka.__super__ = kb;
            ka.prototype = v(kb.prototype, {
                stopDrag: function() {
                    null != this.stage && this.stage.__stopDrag(this)
                },
                __setStageReference: function(a) {
                    this.stage != a && null != this.stage && this.stage.__dragObject == this && this.stopDrag();
                    kb.prototype.__setStageReference.call(this, a)
                },
                __getCursor: function() {
                    return this.__buttonMode && this.useHandCursor ? "button" : null
                },
                __hitTest: function(a, b, c, d, f, h) {
                    if (f && !this.mouseEnabled && !this.mouseChildren) return !1;
                    if (!h.get_visible() || this.__isMask || null != this.get_mask() && !this.get_mask().__hitTestMask(a, b)) return this.__hitTestHitArea(a, b, c, d, f, h);
                    if (null != this.__scrollRect) {
                        var k = I.__pool.get();
                        k.setTo(a, b);
                        var n = this.__getRenderTransform(),
                            p = n.a * n.d - n.b * n.c;
                        if (0 == p) k.x = -n.tx, k.y = -n.ty;
                        else {
                            var P = 1 / p * (n.c * (n.ty - k.y) + n.d * (k.x - n.tx));
                            k.y = 1 / p * (n.a * (k.y - n.ty) + n.b * (n.tx - k.x));
                            k.x = P
                        }
                        if (!this.__scrollRect.containsPoint(k)) return I.__pool.release(k), this.__hitTestHitArea(a, b, c, d, !0, h);
                        I.__pool.release(k)
                    }
                    return kb.prototype.__hitTest.call(this, a, b, c, d, f, h) ? null != d ? f : !0 : null == this.hitArea && null != this.__graphics && this.__graphics.__hitTest(a, b, c, this.__getRenderTransform()) ? (null == d || f && !this.mouseEnabled ||
                        d.push(h), !0) : this.__hitTestHitArea(a, b, c, d, f, h)
                },
                __hitTestHitArea: function(a, b, c, d, f, h) {
                    return null == this.hitArea || this.hitArea.mouseEnabled ? !1 : (this.hitArea.mouseEnabled = !0, a = this.hitArea.__hitTest(a, b, c, null, !0, h), this.hitArea.mouseEnabled = !1, null != d && a && (d[d.length] = h), a)
                },
                __hitTestMask: function(a, b) {
                    return kb.prototype.__hitTestMask.call(this, a, b) || null != this.__graphics && this.__graphics.__hitTest(a, b, !0, this.__getRenderTransform()) ? !0 : !1
                },
                get_graphics: function() {
                    null == this.__graphics && (this.__graphics =
                        new Ed(this));
                    return this.__graphics
                },
                get_tabEnabled: function() {
                    return null == this.__tabEnabled ? this.__buttonMode : this.__tabEnabled
                },
                get_buttonMode: function() {
                    return this.__buttonMode
                },
                set_buttonMode: function(a) {
                    return this.__buttonMode = a
                },
                __class__: ka,
                __properties__: v(kb.prototype.__properties__, {
                    get_graphics: "get_graphics",
                    set_buttonMode: "set_buttonMode",
                    get_buttonMode: "get_buttonMode"
                })
            });
            var bb = function(a) {
                bb.instance = this;
                ka.call(this);
                this.prepareStage();
                rb.useEnterFrame(this);
                bb.switchScene(a)
            };
            g["com.watabou.coogee.Game"] = bb;
            bb.__name__ = "com.watabou.coogee.Game";
            bb.switchScene = function(a) {
                bb.instance.switchSceneImp(a)
            };
            bb.quit = function() {};
            bb.__super__ = ka;
            bb.prototype = v(ka.prototype, {
                prepareStage: function() {
                    var a = this;
                    this.stage.align = 6;
                    this.stage.set_scaleMode(2);
                    this.stage.addEventListener("resize", function(b) {
                        a.layout()
                    });
                    this.stage.application.onExit.add(l(this, this.onExit));
                    this.stage.application.__window.onActivate.add(l(this, this.onResume));
                    this.stage.application.__window.onDeactivate.add(l(this,
                        this.onPause))
                },
                onExit: function(a) {
                    rb.stop()
                },
                onResume: function() {},
                onPause: function() {},
                layout: function() {
                    if (null != bb.scene) {
                        var a = this.stage.stageWidth,
                            b = this.stage.stageHeight,
                            c = this.getScale(a, b);
                        bb.scene.set_scaleX(bb.scene.set_scaleY(c));
                        bb.scene.setSize(a / c, b / c)
                    }
                },
                getScale: function(a, b) {
                    return 1
                },
                switchSceneImp: function(a) {
                    null != bb.scene && (bb.scene.deactivate(), this.removeChild(bb.scene), bb.scene = null);
                    null != a && (bb.scene = w.createInstance(a, []), this.addChild(bb.scene), this.layout(), bb.scene.activate());
                    this.stage.set_focus(this.stage)
                },
                __class__: bb
            });
            var sb = function() {
                C.reset();
                D.useDefault();
                za.baseURL = "https://watabou.github.io/city-generator";
                sb.preview = za.getFlag("preview");
                ba.init(null, kd.prepare);
                K.restore();
                this.stage.showDefaultContextMenu = !1;
                var a = Fd.fromURL();
                null == a ? a = Fd.create(25, C.seed) : null != a.style && K.setPalette(Xc.fromAsset(a.style));
                new Ub(a);
                bb.call(this, Ec)
            };
            g["com.watabou.mfcg.Main"] = sb;
            sb.__name__ = "com.watabou.mfcg.Main";
            sb.__super__ = bb;
            sb.prototype = v(bb.prototype, {
                getScale: function(a,
                    b) {
                    Vj.get_screenDPI();
                    return 1
                },
                switchSceneImp: function(a) {
                    null == u.layer && (u.layer = new U);
                    bb.prototype.switchSceneImp.call(this, a);
                    this.addChild(u.layer)
                },
                layout: function() {
                    var a = this.stage.stageWidth,
                        b = this.stage.stageHeight,
                        c = this.getScale(a, b);
                    u.layer.set_scaleX(u.layer.set_scaleY(c));
                    u.layer.setSize(a / c, b / c);
                    bb.prototype.layout.call(this)
