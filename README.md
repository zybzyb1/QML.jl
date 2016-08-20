# QML
This package provides an interface to [Qt5 QML](http://qt.io/). It uses the [`CxxWrap`](https://github.com/barche/CxxWrap.jl) package to expose C++ classes. Current functionality allows interaction between QML and Julia using basic numerical and string types, as well as display of PNG images and a very experimental OpenGL rendering element (see `example/gltriangle.jl`).

![QML plots example](example/plot.png?raw=true "Plots example")

![OpenGL example](example/gltriangle.gif?raw=true "OpenGL example, using GLAbstraction.jl")

## Installation
This was tested on Linux and OS X. You need `cmake` in your path for installation to work. Building on Windows should also work, see CxxWrap docs for compiler requirements.

First install [`CxxWrap`](https://github.com/barche/CxxWrap.jl) using `Pkg.add`. Compilation of `QML.jl` requires Qt to be reachable by CMake. If it is in a non-standard location, set the environment variable `CMAKE_PREFIX_PATH` to the base Qt directory (the one containing `lib` and `bin`) before executing the following commands:

```julia
Pkg.clone("https://github.com/barche/QML.jl.git")
Pkg.build("QML")
Pkg.test("QML")
```

You can check that the correct Qt version is used using the `qt_prefix_path()` function.

## Usage
### Loading a QML file
We support three methods of loading a QML file: `QQmlApplicationEngine`, `QQuickView` and `QQmlComponent`. These behave equivalently to the corresponding Qt classes.
#### QQmlApplicationEngine
The easiest way to run the QML file `main.qml` from the current directory is using the `@qmlapp` macro:
```julia
using QML
@qmlapp "main.qml"
exec()
```
The QML must have an `ApplicationWindow` as top component. It is also possible to default-construct the `QQmlApplicationEngine` and call `load` to load the QML separately:
```julia
qml_engine = init_qmlapplicationengine()
# ...
# set properties, ...
# ...
load(qml_engine, "main.qml")
```

This is useful to set context properties before loading the QML, for example.

Note we use `init_` functions rather than calling the constructor for the Qt type directly. The init methods have the advantage that cleanup (calling delete etc.) happens in C++ automatically. Calling the constructor directly requires manually finalizing the corresponding components in the correct order and has a high risk of causing crashes on exit.

#### QQuickView
The `QQuickView` creates a window, so it's not necessary to wrap the QML in `ApplicationWindow`. A QML file is loaded as follows:

```julia
qview = init_qquickview()
set_source(qview, "main.qml")
QML.show(qview)
exec()
```

#### QQmlComponent
Using `QQmlComponent` the QML code can be set from a Julia string wrapped in `QByteArray`:

```julia
qml_data = QByteArray("""
import ...

ApplicationWindow {
  ...
}
""")

qengine = init_qmlengine()
qcomp = QQmlComponent(qengine)
set_data(qcomp, qml_data, "")
create(qcomp, qmlcontext());

# Run the application
exec()
```

## Interacting with Julia
Interaction with Julia happens through the following mechanisms:
* Call Julia functions from QML
* Read and set context properties from Julia and QML
* Emit signals from Julia to QML

Note that Julia slots appear missing, but they are not needed since it is possible to directly connect a Julia function to a QML signal in the QML code (see the QTimer example below).

### Calling Julia functions
In Julia, functions are registered using the `@qmlfunction` macro:
```julia
my_function() = "Hello from Julia"
my_other_function(a, b) = "Hi from Julia"

@qmlfunction my_function my_other_function
```

The macro takes any number of function names as arguments

In QML, include the Julia API:
```qml
import org.julialang 1.0
```

Then call a Julia function in QML using:
```qml
Julia.my_function()
Julia.my_other_function(arg1, arg2)
```

### Context properties
The entry point for setting context properties is the root context of the engine, available using the `qmlcontext()` function. It is defined once the `@qmlapp` macro or one of the init functions has been called.
```julia
@qmlset qmlcontext().property_name = property_value
```

This sets the QML context property named `property_name` to value `julia_value`. Any time the `@qmlset` macro is called on such a property, QML is notified of the change and updates any dependent values.

The value of a property can be queried from Julia like this:
```julia
@qmlget qmlcontext().property_name
```

At application initialization, it is also possible to pass context properties as additional arguments to the `@qmlapp` macro:
```julia
my_prop = 2.
@qmlapp "main.qml" my_prop
```
This will initialize a context property named `my_prop` with the value 2.

#### Type conversion
Most fundamental types are converted implicitly. Mind that the default integer type in QML corresponds to `Int32` in Julia

#### Composite types
Setting a composite type as a context property maps the type fields into a `JuliaObject`, which derives from `QQmlPropertyMap`. Example:

```julia
type JuliaTestType
  a::Int32
end

jobj = JuliaTestType(0.)
@qmlset root_ctx.julia_object = jobj
@qmlset root_ctx.julia_object.a = 2
@test @qmlget(root_ctx.julia_object.a) == 2
@test jobj.a == 2
```

Access from QML:
```qml
import QtQuick 2.0

Timer {
     interval: 0; running: true; repeat: false
     onTriggered: {
       julia_object.a = 1
       Qt.quit()
     }
 }
```

### Emitting signals from Julia
Defining signals must be done in QML in the JuliaSignals block, following the instructions from the [QML manual](http://doc.qt.io/qt-5/qtqml-syntax-objectattributes.html#signal-attributes). Example signal with connection:
```qml
JuliaSignals {
  signal fizzBuzzFound(int fizzbuzzvalue)
  onFizzBuzzFound: lastFizzBuzz.text = fizzbuzzvalue
}
```

The above signal is emitted from Julia using simply:
```julia
@emit fizzBuzzFound(i)
```

**There must never be more than one JuliaSignals block in QML**

## Using QTimer
`QTimer` can be used to simulate running Julia code in the background. Excerpts from [`test/gui.jl`](test/gui.jl):

```julia
bg_counter = 0

function counter_slot()
  global bg_counter
  bg_counter += 1
  @qmlset qmlcontext().bg_counter = bg_counter
end

@qmlfunction counter_slot

timer = QTimer()
@qmlset qmlcontext().bg_counter = bg_counter
@qmlset qmlcontext().timer = timer
```

Use in QML like this:
```qml
import QtQuick 2.0
import QtQuick.Controls 1.0
import QtQuick.Layouts 1.0
import org.julialang 1.0

ApplicationWindow {
    title: "My Application"
    width: 480
    height: 640
    visible: true

    Connections {
      target: timer
      onTimeout: Julia.counter_slot()
    }

    ColumnLayout {
      spacing: 6
      anchors.centerIn: parent

      Button {
          Layout.alignment: Qt.AlignCenter
          text: "Start counting"
          onClicked: timer.start()
      }

      Text {
          Layout.alignment: Qt.AlignCenter
          text: bg_counter.toString()
      }

      Button {
          Layout.alignment: Qt.AlignCenter
          text: "Stop counting"
          onClicked: timer.stop()
      }
  }
}

```

Note that QML provides the infrastructure to connect to the `QTimer` signal through the `Connections` item.

## JuliaDisplay
QML.jl provides a custom QML type named `JuliaDisplay` that acts as a standard Julia multimedia `Display`. Currently, only the `image/png` mime type is supported. Example use in QML from the `plot` example:
 ```qml
 JuliaDisplay {
   id: jdisp
   Layout.fillWidth: true
   Layout.fillHeight: true
   onHeightChanged: root.do_plot()
   onWidthChanged: root.do_plot()
 }
 ```
 The function `do_plot` is defined in the parent QML component and calls the Julia plotting routine, passing the display as an argument:
 ```qml
 function do_plot()
 {
   if(jdisp === null)
     return;

   Julia.plotsin(jdisp, jdisp.width, jdisp.height, amplitude.value, frequency.value);
 }
 ```
 Of course the display can also be added using `pushdisplay!`, but passing by value can be more convenient when defining multiple displays in QML.

## Combination with the REPL
When launching the application using `exec`, execution in the REPL will block until the GUI is closed. If you want to continue using the REPL with an active QML gui, `exec_async` provides an alternative. This method keeps the REPL active and polls the QML interface periodically for events, using a timer in the Julia event loop. An example (requiring packages Plots.jl and PyPlot.jl) can be found in `example/repl-background.jl`, to be used as:
```julia
include("example/repl-background.jl")
plot([1,2],[3,4])
```
This should display the result of the plotting command in the QML window.
