use canopus_ui_core::{
    ActionRow, Application, ApplicationRuntime, Command, CommandSink, Layout, NavigationHeader,
    NavigationPage, Router, Snapshot, SnapshotBackend, StatusRow, Style, SwitchRow, Tree, UiError,
    View, view,
};

#[derive(Copy, Clone)]
enum Message {
    Refresh,
    ToggleSafeMode,
    OpenModules,
    Back,
}

impl From<Message> for u32 {
    fn from(message: Message) -> Self {
        match message {
            Message::Refresh => 1,
            Message::ToggleSafeMode => 2,
            Message::OpenModules => 3,
            Message::Back => 4,
        }
    }
}

#[derive(Copy, Clone, PartialEq, Eq)]
enum Route {
    Overview,
    Modules,
}

struct Model {
    status: &'static str,
    safe_mode: bool,
    router: Router<Route, 4>,
}

struct ManagerApp;

impl Application for ManagerApp {
    type Model = Model;
    type Message = Message;

    fn decode_message(event_id: u32) -> Option<Self::Message> {
        match event_id {
            1 => Some(Message::Refresh),
            2 => Some(Message::ToggleSafeMode),
            3 => Some(Message::OpenModules),
            4 => Some(Message::Back),
            _ => None,
        }
    }

    fn update(
        &mut self,
        model: &mut Self::Model,
        message: Self::Message,
        commands: &mut impl CommandSink<Self::Message>,
    ) {
        match message {
            Message::Refresh => model.status = "就绪",
            Message::ToggleSafeMode => {
                model.safe_mode = !model.safe_mode;
                let _ = commands.submit(Command::Rebuild);
            }
            Message::OpenModules => {
                let _ = model.router.push(Route::Modules);
                let _ = commands.submit(Command::Rebuild);
            }
            Message::Back => {
                let _ = model.router.back(model.router.generation());
                let _ = commands.submit(Command::Rebuild);
            }
        }
    }
}

#[derive(Default)]
struct HostBackend {
    apply_count: u32,
}

impl SnapshotBackend for HostBackend {
    fn apply(&mut self, snapshot: &Snapshot) -> Result<(), UiError> {
        self.apply_count += 1;
        println!(
            "apply generation={} nodes={}",
            snapshot.generation, snapshot.node_count
        );
        Ok(())
    }
}

fn render(model: &Model, tree: &mut Tree) -> Result<(), UiError> {
    match model.router.current() {
        Route::Overview => view!(NavigationPage {
            key: 1,
            title: "Canopus 管理器",
            children: (
                NavigationHeader {
                    key: 2,
                    title: "Canopus 管理器",
                    subtitle: "原生框架",
                    back: None,
                    centered: true,
                    elevated: false,
                    style: Style::default(),
                    layout: Layout::default(),
                    children: (),
                },
                StatusRow {
                    key: 3,
                    label: "监督器",
                    value: model.status,
                },
                SwitchRow {
                    key: 4,
                    label: "安全模式",
                    detail: "限制模块启用",
                    event: Message::ToggleSafeMode,
                    checked: model.safe_mode,
                    enabled: true,
                },
                ActionRow {
                    key: 5,
                    label: "模块",
                    detail: "查看已安装模块",
                    event: Message::OpenModules,
                    enabled: true,
                },
                ActionRow {
                    key: 6,
                    label: "刷新",
                    detail: "读取权威状态",
                    event: Message::Refresh,
                    enabled: true,
                },
            ),
        })
        .render(tree),
        Route::Modules => view!(NavigationPage {
            key: 1,
            title: "Canopus 管理器",
            children: (
                NavigationHeader {
                    key: 2,
                    title: "模块",
                    subtitle: "已安装组件",
                    back: Some(Message::Back),
                    centered: true,
                    elevated: false,
                    style: Style::default(),
                    layout: Layout::default(),
                    children: (),
                },
                StatusRow {
                    key: 3,
                    label: "示例模块",
                    value: "运行中",
                },
            ),
        })
        .render(tree),
    }
}

fn main() {
    let model = Model {
        status: "启动中",
        safe_mode: false,
        router: Router::new(Route::Overview).unwrap(),
    };
    let mut runtime: ApplicationRuntime<ManagerApp, HostBackend, 4> =
        ApplicationRuntime::new(ManagerApp, model, HostBackend::default());

    runtime.rebuild(render).unwrap();
    let generation = runtime.current().generation;
    runtime
        .dispatch_event(generation, 4, Message::ToggleSafeMode.into(), render)
        .unwrap();
}
