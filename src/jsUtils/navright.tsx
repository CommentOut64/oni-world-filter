import React from "react";
import { useContext, useRef, useState } from "react";
import Button from "react-bootstrap/Button";
import ListGroup from "react-bootstrap/ListGroup";
import Overlay from "react-bootstrap/Overlay";
import Popover from "react-bootstrap/Popover";
import Translate from "react-bootstrap-icons/dist/icons/translate";
import Sun from "react-bootstrap-icons/dist/icons/sun";
import MoonStars from "react-bootstrap-icons/dist/icons/moon-stars";
import Github from "react-bootstrap-icons/dist/icons/github";

import configuration from "./configuration";
import { LanguageContext, useTranslation } from "./language";
import { ThemeContext } from "./index";

interface NavRightProps {
    onSetAppConfig: (lang: string, theme: number) => void;
}

const NavRight = ({ onSetAppConfig }: NavRightProps) => {
    const [show, setShow] = useState(false);
    const target = useRef(null);
    const theme = useContext(ThemeContext);
    const language = useContext(LanguageContext);
    const translation = useTranslation();
    return (
        <>
            <Button
                variant="secondary"
                ref={target}
                className="round-button"
                onClick={() => setShow(!show)}
            >
                <Translate size={24} />
            </Button>
            <Overlay
                target={target.current}
                show={show}
                placement="bottom-start"
                rootClose={true}
                rootCloseEvent="click"
                onHide={() => setShow(false)}
            >
                <Popover>
                    <ListGroup>
                        {configuration.languages.map((item) => (
                            <ListGroup.Item
                                key={item.key}
                                active={language === item.key}
                                action
                                onClick={() => {
                                    setShow(false);
                                    onSetAppConfig(item.key, theme);
                                }}
                            >
                                {item.name}
                            </ListGroup.Item>
                        ))}
                    </ListGroup>
                </Popover>
            </Overlay>
            <Button
                variant="secondary"
                className="round-button"
                onClick={() => onSetAppConfig(language, theme ? 0 : 1)}
                title={translation(
                    `Toggle to ${theme ? "Light" : "Dark"} mode`
                )}
            >
                <MoonStars
                    size={24}
                    style={{ display: theme ? "none" : "inline" }}
                />
                <Sun size={24} style={{ display: theme ? "inline" : "none" }} />
            </Button>
            <Button
                as="a"
                variant="secondary"
                href="https://github.com/genrwoody/oni_world_app"
                target="_blank"
                title={translation("Open on GitHub")}
                className="round-button"
            >
                <Github size={24} />
            </Button>
            <span>{process.env.VERSION}</span>
        </>
    );
};

export default NavRight;
