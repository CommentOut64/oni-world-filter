import React from "react";
import { useState, useEffect, useContext } from "react";
import { createRoot } from "react-dom/client";
import Navbar from "react-bootstrap/Navbar";
import Container from "react-bootstrap/Container";
import Stack from "react-bootstrap/Stack";
import Row from "react-bootstrap/Row";
import Col from "react-bootstrap/Col";
import Card from "react-bootstrap/Card";
import Modal from "react-bootstrap/Modal";
import Tabs from "react-bootstrap/Tabs";
import Tab from "react-bootstrap/Tab";

import { LanguageContext, useTranslation } from "./jsUtils/language";
import configuration from "./jsUtils/configuration";
import WorldCanvas from "./jsUtils/worldcanvas";
import ToolBar from "./jsUtils/toolbar";
import NavRight from "./jsUtils/navright";
import { updateWorld, ThemeContext } from "./jsUtils";

import "./jsUtils/index.css";
import zonesImageUrl from "../asset/zones.png";

interface WorldInfoProps {
    world: World;
    onSetFocus: (index: number) => void;
}

const WorldInfo = ({ world, onSetFocus }: WorldInfoProps) => {
    const translation = useTranslation();
    const convert = (color?: number) => {
        const list = ["success", "danger", "primary"];
        return color ? list[color - 1] : undefined;
    };
    const traits = configuration.traits;
    return (
        <>
            <Row xs={2} md={4}>
                {world.traits.map((item, index) => (
                    <Card key={index} text={convert(traits[item].type)}>
                        <Card.Body style={{ padding: "0.75rem 0" }}>
                            {translation(traits[item].name)}
                        </Card.Body>
                    </Card>
                ))}
            </Row>
            <Row xs={2} md={4}>
                {world.geysers.map((item, index) => (
                    <Card
                        key={index}
                        text={convert(item.desc.type)}
                        onMouseEnter={() => onSetFocus(index)}
                        onMouseLeave={() => onSetFocus(-1)}
                    >
                        <Card.Body style={{ padding: "0.75rem 0" }}>
                            {translation(item.desc.name)}
                        </Card.Body>
                    </Card>
                ))}
            </Row>
        </>
    );
};

const Biomes = () => {
    const biomes = [
        { name: "Tundra Biome", iconIndex: 0 },
        { name: "Marsh Biome", iconIndex: 2 },
        { name: "Sandstone Biome", iconIndex: 3 },
        { name: "Jungle Biome", iconIndex: 4 },
        { name: "Magma Biome", iconIndex: 5 },
        { name: "Oily Biome", iconIndex: 6 },
        { name: "Space Biome", iconIndex: 7 },
        { name: "Ocean Biome", iconIndex: 8 },
        { name: "Rust Biome", iconIndex: 9 },
        { name: "Forest Biome", iconIndex: 10 },
        { name: "Radioactive Biome", iconIndex: 11 },
        { name: "Swampy Biome", iconIndex: 12 },
        { name: "Wasteland Biome", iconIndex: 13 },
        { name: "Metallic Biome", iconIndex: 15 },
        { name: "Barren Biome", iconIndex: 16 },
        { name: "Moo Biome", iconIndex: 17 },
        { name: "Ice Cave Biome", iconIndex: 18 },
        { name: "Cool Pool Biome", iconIndex: 19 },
        { name: "Nectar Biome", iconIndex: 20 },
        { name: "Aquatic Biome" },
        { name: "Niobium Biome" },
        { name: "Regolith Biome" },
        { name: "Garden Biome", iconIndex: 21 },
        { name: "Feather Biome", iconIndex: 22 },
        { name: "Wetlands Biome", iconIndex: 23 },
    ];
    const translation = useTranslation();
    return (
        <Row xs={3}>
            {biomes.map((item) => (
                <Card key={item.name}>
                    <Card.Body style={{ padding: "0.75rem 0" }}>
                        {item.iconIndex !== undefined ? (
                            <span
                                className={"biome-icon icon" + item.iconIndex}
                            ></span>
                        ) : null}
                        <span>{translation(item.name)}</span>
                    </Card.Body>
                </Card>
            ))}
        </Row>
    );
};

const createSprite = (e: Event) => {
    const promises = [];
    const image = e.target as HTMLImageElement;
    for (let i = 0; i < 5; i++) {
        for (let j = 0; j < 5; j++) {
            const promise = createImageBitmap(image, j * 32, i * 32, 32, 32);
            promises.push(promise);
        }
    }
    Promise.all(promises).then((sprites) => Module.sprite.push(...sprites));
};

interface AppProps {
    onSetLanguage: (lang: string) => void;
    onSetTheme: (theme: number) => void;
}

const App = ({ onSetLanguage, onSetTheme }: AppProps) => {
    const [loading, setLoading] = useState(true);
    const [worlds, setWorlds] = useState(new Array<World>());
    const [focus, setFocus] = useState(-1);
    const language = useContext(LanguageContext);
    const translation = useTranslation();
    useEffect(() => {
        document.title = translation("ONI World Generator");
    }, [language]);
    useEffect(() => {
        if (Module.wasm !== undefined) return;
        Module.wasm = null;
        Module.worlds = [];
        Module.sprite = [];
        Module.updateWorld = updateWorld;
        Module.onRuntimeInitialized = () => {
            Module.app_init(new Date().getTime() & 0x7fffffff);
            setLoading(false);
        };
        setLoading(true);
        const load = async (url: string) => {
            const response = await fetch(url, { credentials: "same-origin" });
            return response.arrayBuffer();
        };
        import("@generated-wasm-files").then((module) => {
            Promise.all([
                load(module.WasmFiles.data),
                load(module.WasmFiles.wasm),
            ])
                .then((buffers) => {
                    Module.data = new Uint8Array(buffers[0], 4);
                    Module.wasm = new Uint8Array(buffers[1]);
                    const script = document.createElement("script");
                    script.src = module.WasmFiles.launcher;
                    script.async = true;
                    document.body.appendChild(script);
                })
                .catch((reason) => console.log("fetch error: " + reason));
        });
        const image = new Image();
        image.onload = createSprite;
        image.src = zonesImageUrl;
        //if ("serviceWorker" in navigator) {
        //    navigator.serviceWorker.register("./serviceworker.js");
        //}
    }, []);
    const onSetWorlds = () => {
        setFocus(-1);
        if (Module.worlds.length === 0) {
            return;
        }
        if (Module.worlds[0].type !== 0) {
            setWorlds([Module.worlds[1], Module.worlds[0]]);
        } else {
            setWorlds([...Module.worlds]);
        }
    };
    const onSetAppConfig = (lang: string, theme: number) => {
        onSetLanguage(lang);
        onSetTheme(theme);
    };
    const onSetFocus = (world: number, geyser: number) => {
        if (geyser === -1) {
            setFocus(-1);
            return;
        }
        let offset = 0;
        for (let i = 0; i < world; i++) {
            offset += worlds[i].geysers.length;
        }
        setFocus(geyser + offset);
    };
    const tabsElement = () => {
        return (
            <Tabs defaultActiveKey="info" className="mb-3">
                <Tab eventKey="info" title={translation("Information")}>
                    {worlds.map((world, index) => (
                        <WorldInfo
                            key={index}
                            world={world}
                            onSetFocus={(geyser) => onSetFocus(index, geyser)}
                        />
                    ))}
                </Tab>
                <Tab eventKey="biome" title={translation("Biomes")}>
                    <Biomes />
                </Tab>
            </Tabs>
        );
    };
    return (
        <>
            <Navbar className="bg-body-tertiary justify-content-between">
                <Container>
                    <Stack direction="horizontal">
                        <ToolBar
                            onSetAppConfig={onSetAppConfig}
                            onSetWorld={onSetWorlds}
                        />
                    </Stack>
                    <Stack
                        direction="horizontal"
                        className="d-none d-md-flex"
                        gap={3}
                    >
                        <NavRight onSetAppConfig={onSetAppConfig} />
                    </Stack>
                </Container>
            </Navbar>
            <Container>
                <Row>
                    <Col lg={12} xl={6}>
                        {tabsElement()}
                    </Col>
                    <WorldCanvas worlds={worlds} focus={focus} />
                </Row>
            </Container>
            <Modal
                id="loading"
                show={loading}
                backdrop="static"
                keyboard={false}
                centered
            >
                <Modal.Body>
                    {translation("Initializing, please wait a moment.")}
                </Modal.Body>
            </Modal>
        </>
    );
};

const Main: React.FC = () => {
    const initTheme = (): number => {
        return matchMedia("(prefers-color-scheme: dark)").matches ? 1 : 0;
    };
    const [language, setLanguage] = useState(navigator.language);
    const [theme, setTheme] = useState(initTheme());
    useEffect(() => {
        const expect = theme === 0 ? "light" : "dark";
        document.documentElement.setAttribute("data-bs-theme", expect);
    }, [theme]);
    const onSetTheme = (theme: number) => {
        const expect = theme === 0 ? "light" : "dark";
        document.documentElement.setAttribute("data-bs-theme", expect);
        setTheme(theme);
    };
    return (
        <ThemeContext.Provider value={theme}>
            <LanguageContext.Provider value={language}>
                <App onSetLanguage={setLanguage} onSetTheme={onSetTheme} />
            </LanguageContext.Provider>
        </ThemeContext.Provider>
    );
};

const root = createRoot(document.getElementById("root")!);
if (process.env.NODE_ENV === "development") {
    root.render(
        <React.StrictMode>
            <Main />
        </React.StrictMode>
    );
} else {
    root.render(<Main />);
}
