import type { DepItemsResult } from "./types";
import { PxModRimRPC } from "./bridge";

let _rpc: PxModRimRPC | null = null;

export function initAPI(rpc: PxModRimRPC): void {
    _rpc = rpc;
}

export namespace PxModRimAPI {
    export async function initReady(): Promise<void> {
        await _rpc!.call("init_ready");
    }

    export async function toggleDownloadChecked(
        modId: string,
        title: string,
        checked: boolean,
    ): Promise<void> {
        await _rpc!.call("toggle_download_checked", {
            mod_id: modId,
            title,
            checked,
        });
    }

    export async function batchToggleDownloadChecked(
        modIds: string[],
        titles: string[],
        checked: boolean,
    ): Promise<void> {
        await _rpc!.call("batch_toggle_download_checked", {
            mod_ids: modIds,
            titles,
            checked,
        });
    }

    export async function toggleActive(
        modId: string,
        active: boolean,
    ): Promise<void> {
        await _rpc!.call("toggle_active", {
            mod_id: modId,
            active,
        });
    }

    export async function batchToggleActive(
        modIds: string[],
        active: boolean,
    ): Promise<void> {
        await _rpc!.call("batch_toggle_active", {
            mod_ids: modIds,
            active,
        });
    }

    export async function fetchModDeps(
        modId: string,
    ): Promise<DepItemsResult | null> {
        const r = await _rpc!.call("fetch_mod_deps", { mod_id: modId });
        if (!r.ok) {
            console.error("[pxmodrim] fetchModDeps failed:", r.error);
            return null;
        }
        return r.result ?? null;
    }
}
