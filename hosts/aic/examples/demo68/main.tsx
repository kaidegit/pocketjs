import Hero from "../../../../apps/hero/app.tsx";
import { mount } from "@pocketjs/framework/solid";
import { TICKS_PER_SECOND } from "@pocketjs/framework/clock";

mount(() => <Hero presentationHz={TICKS_PER_SECOND} />);
